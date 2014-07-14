all:
	@echo "Either make linux or make akaros"

linux:
	gcc -std=gnu99 kweb.c tpool.c -o kweb -lpthread

akaros:
	x86_64-ros-gcc -std=gnu99 kweb.c tpool.c -o kweb -lpthread -lbenchutil -lbsd
