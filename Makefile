FILES = kweb.c kqueue.c tpool.c cpu_util.c ktimer.c kstats.c urlcmd.c

all:
	@echo "Either make linux or make akaros"

linux:
	gcc -std=gnu99 $(FILES) linux.c -o kweb -lpthread

akaros:
	x86_64-ros-gcc -std=gnu99 $(FILES) akaros.c -o kweb -lpthread -lbenchutil -lbsd

clean:
	rm -rf kweb
