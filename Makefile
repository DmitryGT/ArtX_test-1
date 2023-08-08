all:test1 test2 test3

test1_src += \
	test1.c

test1_obj := $(patsubst %.c, %.o, $(test1_src))

test2_src += \
	test2.c

test2_obj := $(patsubst %.c, %.o, $(test2_src))

test3_src += \
	test3.c

test3_obj := $(patsubst %.c, %.o, $(test3_src))

CC := gcc

CFLAGS += -Wall -fPIC

test1:$(test1_obj)
	$(CC) -o $@ $^ -lev
%.o:%.c
	$(CC) -o $@ -c $< $(CFLAGS)

test2:$(test2_obj)
	$(CC) -o $@ $^ -lev -lpthread
%.o:%.c
	$(CC) -o $@ -c $< $(CFLAGS)

test3:$(test3_obj)
	$(CC) -o $@ $^ -lev -lpthread
%.o:%.c
	$(CC) -o $@ -c $< $(CFLAGS)

.PHONY:clean all

clean:
	@rm -rf test1 test2 test3 *.o