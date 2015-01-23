COMMON_FILES = kweb.c kqueue.c cpu_util.c urlcmd.c
NON_CUSTOM_SCHED_FILES = $(COMMON_FILES) tpool.c kstats.c ktimer.c 

LINUX_NATIVE_FILES = linux.c $(NON_CUSTOM_SCHED_FILES)
LINUX_UPTHREAD_FILES = linux-upthread.c $(NON_CUSTOM_SCHED_FILES)
LINUX_CUSTOM_SCHED_FILES = linux-custom-sched.c $(COMMON_FILES)
AKAROS_FILES = akaros.c $(NON_CUSTOM_SCHED_FILES)
AKAROS_CUSTOM_SCHED_FILES = akaros-custom-sched.c $(COMMON_FILES)

all:
	@echo "Either" \
	      "make linux," \
	      "make linux-upthread," \
	      "make linux-custom-sched," \
	      "make akaros,"
	      "or make akaros-custom-sched," \

linux:
	gcc -std=gnu99 $(LINUX_NATIVE_FILES) -o kweb -lpthread

linux-upthread:
	gcc -g -std=gnu99 $(LINUX_UPTHREAD_FILES) \
	    -DWITH_PARLIB -DWITH_UPTHREAD -o kweb -lupthread -lparlib \
	    -Wl,-wrap,socket \
	    -Wl,-wrap,accept

linux-custom-sched:
	gcc -std=gnu99 $(LINUX_CUSTOM_SCHED_FILES) \
	    -DWITH_PARLIB -DWITH_CUSTOM_SCHED -o kweb -lparlib

akaros:
	x86_64-ucb-akaros-gcc -std=gnu99 $(AKAROS_FILES) \
	    -o kweb -lpthread -lbenchutil -lbsd

akaros-custom-sched:
	x86_64-ucb-akaros-gcc -std=gnu99 $(AKAROS_CUSTOM_SCHED_FILES) \
	    -DWITH_CUSTOM_SCHED -o kweb -lpthread -lbenchutil -lbsd

clean:
	rm -rf kweb
