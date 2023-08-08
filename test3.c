#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ev.h>
#include <pthread.h>
#include <netinet/udp.h>	
#include <netinet/ip.h>


#define DEBUG	0

#define printError( message )  do { perror( message ); } while (0)
#define fatalError( message )  do { perror( message ); exit(EXIT_FAILURE); } while (0)

#define MAX_DATA_LEN	65535

struct {
	pthread_mutex_t mutex;				// mutex
	pthread_cond_t  cond; 				// Условная переменная
	unsigned char	*data;				// Данные для обработки
	int				size;				// Размер данных
} data = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, NULL, -1};

int sock_in, sock_out;

void modify_UDP_data( void );


//---------------------------------------------------------
// Обработка данных
//---------------------------------------------------------
void processing_data( unsigned char *data_in, int size ) {

	// Данные
	data.data = data_in; 
	data.size = size;
	// Посылаем сигнал о наличии данных для обработки
	pthread_cond_signal(&data.cond);
	// Ждем сигнала окончания обработки данных
	pthread_cond_wait(&data.cond,&data.mutex);
}


//---------------------------------------------------------
// Обработка пакета
//---------------------------------------------------------
void UDPread_cb(struct ev_loop *loop, ev_io *watcher, int revents) {

    int addr_size,data_size_in,data_size_out;
	struct sockaddr_in addr;
    unsigned char buffer[MAX_DATA_LEN];

	addr_size = sizeof(addr);
	data_size_in = recvfrom(sock_in, buffer, MAX_DATA_LEN, 0, &addr , &addr_size);
	if ( data_size_in < 0 ) {
			printError("Error receive packet");
			return;
	}
	processing_data((unsigned char*)&buffer,data_size_in);
	// Отправка обработанного пакета
	data_size_out = sendto(sock_out, buffer, data_size_in, 0, &addr , addr_size);
	if ( data_size_out < 0 ) {
		printError("Error send packet");
		return;
	}
	if ( data_size_out != data_size_in ) {
		printf("Send error: size packet mismatch\n");
		return;
	}

}

//---------------------------------------------------------
// Цикл ловли входящих пакетов UDP
//---------------------------------------------------------
void *UDP_sniff( void *arg ) {

	// Создание цикла событий
	struct ev_loop *loop = EV_DEFAULT;

	// Создаём наблюдателя за появлением данных на сокете
	ev_io *onlooker = calloc(1, sizeof(*onlooker));
	if( !onlooker )
		fatalError("Can not alloc memory for onlooker");
	// инициализация обработчика события 
	ev_io_init(onlooker, UDPread_cb, sock_in, EV_READ);
	ev_io_start(loop, onlooker);
	printf("Start sniffer in thread:%ld\n",pthread_self());
	// запуск цикла обработки событий
	ev_run(loop, 0);
}

//---------------------------------------------------------
// Tочка входа
//---------------------------------------------------------
int main( int argc, char **argv ) {

    // Аргументы
    if ( argc < 3 ) {
        printf("Usage: %s <interface in> <interface out>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
	// Проверка интерфейсов
    if ( if_nametoindex(argv[1]) == 0 ) {
        printf("Error. Interface: %s not found !!!\n", argv[1]);
        exit(EXIT_FAILURE);
	}
    if ( if_nametoindex(argv[2]) == 0 ) {
        printf("Error. Interface: %s not found !!!\n", argv[2]);
        exit(EXIT_FAILURE);
    }   

	char *interface_name_in  = argv[1];
	char *interface_name_out = argv[2];

	// Создаем сокет для приема 
	if ( (sock_in = socket(AF_INET , SOCK_RAW , IPPROTO_UDP) ) < 0 )
		fatalError("Socket Error\n");
	// Привязываем к интерфейсу
    if ( setsockopt(sock_in, SOL_SOCKET, SO_BINDTODEVICE, interface_name_in, strlen(interface_name_in)) == -1 ) 
        fatalError("Error bind in socket to interface");

	// Создаем сокет для передачи 
	if ( (sock_out = socket(AF_INET , SOCK_RAW , IPPROTO_UDP) ) < 0 )
		fatalError("Socket Error\n");
	// Привязываем к интерфейсу
    if ( setsockopt(sock_out, SOL_SOCKET, SO_BINDTODEVICE, interface_name_out, strlen(interface_name_out)) == -1 ) 
        fatalError("Error bind out socket to interface");

	// Принимаем пакеты в другом потоке
    pthread_t thread_connect;
    if( pthread_create( &thread_connect, NULL, UDP_sniff, (void *)NULL ) != 0 )
		fatalError("Error create thread");

	// Цикл обработки
	while ( 1 ) {
		// Ждем сигнал для обработки данных
		pthread_cond_wait(&data.cond,&data.mutex);
		// Обработка данных
		modify_UDP_data();
		// Посылаем сигнал готовности данных
		pthread_cond_signal(&data.cond);
	}
	
	return 0;
}

//---------------------------------------------------------
// Реверс данных
//---------------------------------------------------------
void reverse( unsigned char *str, int size ) {
	char* end_ptr = str + (size-1);
	while ( end_ptr > str ) {
		char ch = *str;
		*str = *end_ptr;
		*end_ptr = ch;
		++str, --end_ptr;
	}
}
//---------------------------------------------------------
// Подсчет контрольной суммы
// Источник: https://svnweb.freebsd.org/base/head/sbin/dhclient/packet.c?view=markup
//---------------------------------------------------------
u_int32_t checksum(unsigned char *buf, unsigned nbytes, u_int32_t sum ) {
	unsigned i;

	for (i = 0; i < ( nbytes & ~1U ); i += 2 ) {
		sum += (u_int16_t)ntohs(*((u_int16_t *)(buf + i)));
		if (sum > 0xFFFF)
			sum -= 0xFFFF;
	}

	if ( i < nbytes ) {
		sum += buf[i] << 8;
		if (sum > 0xFFFF)
			sum -= 0xFFFF;
	}

	return (sum);
}

u_int32_t wrapsum(u_int32_t sum) {
	sum = ~sum & 0xFFFF;
	return (htons(sum));
}

//---------------------------------------------------------
// Модификация данных UDP пакета
//---------------------------------------------------------
void modify_UDP_data( void ) {

	// Т.к. мы не меняем длину данных, только зеркалим, нужно изменить сами данные и контрольную сумму
	// в заголовке UDP. Все остальное не трогаем

	struct iphdr  *ip_head  = (struct iphdr *)data.data;
	struct udphdr *udp_head = (struct udphdr*)(data.data + sizeof(struct iphdr));
	unsigned char *udp_data = data.data + sizeof(struct iphdr) + sizeof(struct udphdr);
	
	// Проверка размера пакета на предмет целостности
	if( htons(ip_head->tot_len) != data.size ) {
		printf("Packet size mismatch !\n");
		return;
	}
	
	// Длина данных
	int data_len = htons(udp_head->len) - sizeof(struct udphdr);
#if DEBUG
printf("UDP|Source Port      : %d\n",ntohs(udp_head->source));
printf("UDP|Destination Port : %d\n",ntohs(udp_head->dest));
printf("UDP|Length           : %d\n",ntohs(udp_head->len));
printf("UDP|Checksum         : %d\n",ntohs(udp_head->check));
printf("	Data len: %d Data:%s\n",data_len,udp_data);
#endif
    // Реверсим данные
	reverse(udp_data,data_len);
	// Меняем контрольную сумму в заголовке UDP
	// Источник: https://svnweb.freebsd.org/base/head/sbin/dhclient/packet.c?view=markup
	udp_head->uh_sum = 0;
	udp_head->uh_sum = wrapsum( checksum((unsigned char*)udp_head, sizeof(struct udphdr), 
                                	checksum(udp_data, data_len, 
                                    	checksum((unsigned char*)&ip_head->saddr, 2 * sizeof(ip_head->saddr), IPPROTO_UDP + (u_int32_t)ntohs(udp_head->uh_ulen))
                                	)
                            	)
                        	);
#if DEBUG
printf("-------------- modify --------------\n");
printf("UDP|Source Port      : %d\n",ntohs(udp_head->source));
printf("UDP|Destination Port : %d\n",ntohs(udp_head->dest));
printf("UDP|Length           : %d\n",ntohs(udp_head->len));
printf("UDP|Checksum         : %d\n",ntohs(udp_head->check));
printf("	Data len: %d Data:%s\n",data_len,udp_data);
#endif

}
