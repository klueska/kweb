COMMON_FILES = kweb.c kqueue.c cpu_util.c kstats.c ktimer.c urlcmd.c
LINUX_NATIVE_FILES = linux.c tpool.c
LINUX_UPTHREAD_FILES = linux-upthread.c tpool.c
LINUX_CUSTOM_SCHED_FILES = linux-custom-sched.c
AKAROS_FILES = akaros.c tpool.c

all:
	@echo "Either" \
	      "make linux," \
				"make linux-upthread," \
	      "make linux-custom-sched," \
	      "or make akaros"

linux:
	gcc -std=gnu99 $(COMMON_FILES) $(LINUX_NATIVE_FILES) -o kweb -lpthread

linux-upthread:
	gcc -g -std=gnu99 $(COMMON_FILES) $(LINUX_UPTHREAD_FILES) \
	    -DWITH_PARLIB -DWITH_UPTHREAD -o kweb -lupthread -lparlib \
	    -Wl,-wrap,socket \
	    -Wl,-wrap,accept

linux-custom-sched:
	gcc -std=gnu99 $(COMMON_FILES) $(LINUX_CUSTOM_SCHED_FILES) \
	    -DWITH_PARLIB -DWITH_CUSTOM_SCHED -o kweb -lparlib

akaros:
	x86_64-ros-gcc -std=gnu99 $(COMMON_FILES) $(AKAROS_FILES) \
	    -o kweb -lpthread -lbenchutil -lbsd

clean:
	rm -rf kweb
