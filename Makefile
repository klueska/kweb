FILES = kweb.c request_queue.c tpool.c cpu_util.c

all:
	gcc -std=gnu99 $(FILES) -o kweb -lpthread
