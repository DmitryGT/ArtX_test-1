#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ev.h>
#include <pthread.h>



#define printError( message )  do { perror( message ); } while (0)
#define fatalError( message )  do { perror( message ); exit(EXIT_FAILURE); } while (0)

#define MAX_DATA_LEN	100

struct {
	pthread_mutex_t mutex;				// mutex
	pthread_cond_t  cond; 				//
	char	*data;						// Данные для обработки
	int		size;						// Размер данных
} data = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, NULL, -1};

//---------------------------------------------------------
// Обработка данных
//---------------------------------------------------------
void processing_data( char *data_in, int size ) {

	// Данные
	data.data = data_in; 
	data.size = size;
	// Посылаем сигнал о наличии данных для обработки
	pthread_cond_signal(&data.cond);
	// Ждем сигнала окончания обработки данных
	pthread_cond_wait(&data.cond,&data.mutex);
}

//---------------------------------------------------------
// Чтение данных
//---------------------------------------------------------
void read_cb(struct ev_loop *loop, ev_io *watcher, int revents) {
	ssize_t ret;
	char buf[MAX_DATA_LEN] = {0};
	int cur_fd = watcher->fd;

	ret = recv(cur_fd, buf, sizeof(buf) - 1, 0);
	if ( (ret < 0) && (errno == EAGAIN || errno == EWOULDBLOCK) )
		return;	
	if ( ret > 0 ) {
//		printf("Client (fd:%i) send: %s",cur_fd, buf);
		if( !strstr(buf, "bye") ) {
			processing_data( (char *)&buf, ret );
			send(cur_fd, buf, ret, 0);
			return;
		}
	}
	printf("Close client connection fd:%i\n", cur_fd);
	ev_io_stop(loop, watcher);
	close(cur_fd);
	free(watcher);
}

//---------------------------------------------------------
// Обработка соединения
//---------------------------------------------------------
void connect_cb(struct ev_loop *loop, ev_io *watcher, int revents) {

    int conn = accept( watcher->fd, NULL, NULL );
	if ( ( conn < 0 ) && ( errno == EAGAIN || errno == EWOULDBLOCK ) ) {
		printError("Error accept connection");
		return;
	}
	if ( conn > 0 ) {
		printf("Accept client connection fd:%i\n",conn);
		// Создаём наблюдателя за событием принятой сессии
		ev_io *event = calloc(1, sizeof(*event));
        ev_io_init(event, read_cb, conn, EV_READ);
		ev_io_start(loop, event);
	} 
	else {
		close(watcher->fd);
		ev_break(loop, EVBREAK_ALL);
	}
}

//---------------------------------------------------------
// Цикл событий принятия соединений
//---------------------------------------------------------
void *UDP_sniff( void *arg ) {

	int fd = (int)arg;

	// Создание цикла событий
	struct ev_loop *loop = EV_DEFAULT;

	// Создаём наблюдателя за принятыми соединениями
	ev_io *onlooker = calloc(1, sizeof(*onlooker));
	if( !onlooker )
		fatalError("Can not alloc memory for onlooker");
	// инициализация обработчика события чтения
	ev_io_init(onlooker, connect_cb, fd, EV_READ);
	ev_io_start(loop, onlooker);
	printf("Start listen in thread\n");
	// запуск цикла обработки событий
	ev_run(loop, 0);
}

//---------------------------------------------------------
// Реверс данных
//---------------------------------------------------------
void reverse(char* str, int size)
{
	char* end_ptr = str + (size-1);
	while ( end_ptr > str ) {
		char ch = *str;
		*str = *end_ptr;
		*end_ptr = ch;
		++str, --end_ptr;
	}
}

//---------------------------------------------------------
// Tочка входа
//---------------------------------------------------------
int main( int argc, char **argv ) {

	// Аргументы
    if( argc < 2 ) {
        printf("Usage: %s <port>\n", argv[0]);
        exit(1);
    }
	int port = atoi(argv[1]);

    // Создаём сокет сервера 
	int fd;
	if ( (fd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) 
		fatalError("Error create socket");

	// Неблокирующий режим
	if( fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) == -1 )
		fatalError("Error call fcntl");

	// Повторная привязка
    int res = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &res, sizeof(res));

	// Привязка сокета к адресу 
	struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);
	if ( bind( fd, (struct sockaddr *)&addr, sizeof(addr) ) < 0 )  
		fatalError("Error bind");

	// Прослушка сокета
	if ( listen(fd, 100) < 0 ) 
		fatalError("Error listen");

	// Принимаем и обрабатываем соединения в другом потоке
    pthread_t thread_connect;
    if( pthread_create( &thread_connect, NULL, UDP_sniff, (void *)fd ) != 0 )
		fatalError("Error create thread");

	// Цикл обработки
	while ( 1 ) {
		// Ждем сигнал для обработки данных
		pthread_cond_wait(&data.cond,&data.mutex);
		// Обработка данных
		reverse(data.data,data.size);
		// Посылаем сигнал готовности данных
		pthread_cond_signal(&data.cond);
	}
	
	return 0;
}

