COMMON_FILES = kweb.c kqueue.c cpu_util.c urlcmd.c
NON_CUSTOM_SCHED_FILES = $(COMMON_FILES) tpool.c kstats.c ktimer.c 
THUMBNAIL_FILES = thumbnails.c
THUMBNAIL_LIBS = -lepeg -larchive

LINUX_NATIVE_FILES = linux.c $(THUMBNAIL_FILES) $(NON_CUSTOM_SCHED_FILES)
LINUX_UPTHREAD_FILES = linux-upthread.c $(THUMBNAIL_FILES) $(NON_CUSTOM_SCHED_FILES)
LINUX_CUSTOM_SCHED_FILES = linux-custom-sched.c $(COMMON_FILES)
AKAROS_FILES = akaros.c $(NON_CUSTOM_SCHED_FILES)
AKAROS_CUSTOM_SCHED_FILES = akaros-custom-sched.c $(COMMON_FILES)

all:
	@echo "Must run one of:\n" \
	      "\tmake linux\n" \
	      "\tmake linux-upthread\n" \
	      "\tmake linux-upthread-lithe\n" \
	      "\tmake linux-custom-sched\n" \
	      "\tmake akaros\n" \
	      "\tmake akaros-custom-sched"

linux:
	gcc -std=gnu99 $(LINUX_NATIVE_FILES) -o kweb $(THUMBNAIL_LIBS) -lpthread

linux-upthread:
	gcc -g -std=gnu99 $(LINUX_UPTHREAD_FILES) \
	    -I/usr/include/upthread/compatibility \
	    -DWITH_PARLIB -DWITH_UPTHREAD -o kweb \
	    $(THUMBNAIL_LIBS) -lupthread -lparlib \
	    -Wl,-wrap,socket \
	    -Wl,-wrap,accept

linux-upthread-lithe:
	gcc -g -std=gnu99 $(LINUX_UPTHREAD_FILES) \
	    -I/usr/include/upthread-lithe/compatibility \
	    -DWITH_PARLIB -DWITH_UPTHREAD -DWITH_LITHE -o kweb \
	    $(THUMBNAIL_LIBS) -lupthread-lithe -lithe -lparlib \
	    -Wl,-wrap,socket \
	    -Wl,-wrap,accept

linux-custom-sched:
	gcc -g -std=gnu99 $(LINUX_CUSTOM_SCHED_FILES) \
	    -DWITH_PARLIB -DWITH_CUSTOM_SCHED -o kweb \
	    -lparlib \
	    -Wl,-wrap,socket \
	    -Wl,-wrap,accept

akaros:
	x86_64-ucb-akaros-gcc -std=gnu99 $(AKAROS_FILES) \
	    -o kweb -lpthread -lbenchutil -lbsd

akaros-custom-sched:
	x86_64-ucb-akaros-gcc -std=gnu99 $(AKAROS_CUSTOM_SCHED_FILES) \
	    -DWITH_CUSTOM_SCHED -o kweb -lpthread -lbenchutil -lbsd

clean:
	rm -rf kweb
