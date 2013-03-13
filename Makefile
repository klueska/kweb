all:
	gcc -std=gnu99 kweb.c tpool.c -o kweb -lpthread
