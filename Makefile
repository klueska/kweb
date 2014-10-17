FILES = kweb.c kqueue.c cpu_util.c urlcmd.c
LIN_FILES = $(FILES) tpool.c kstats.c ktimer.c 
AKA_FILES = $(FILES)

all:
	@echo "Either make linux or make akaros"

linux:
	gcc -std=gnu99 $(LIN_FILES) linux.c -o kweb -lpthread

akaros:
	x86_64-ros-gcc -std=gnu99 $(AKA_FILES) akaros.c -o kweb -lpthread -lbenchutil -lbsd

clean:
	rm -rf kweb
