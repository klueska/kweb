FILES = kweb.c kqueue.c tpool.c cpu_util.c ktimer.c

all:
	gcc -std=gnu99 $(FILES) -o kweb -lpthread

clean:
	rm -rf kweb
