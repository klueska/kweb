COMMON_FILES = kweb.c kqueue.c cpu_util.c urlcmd.c
NON_CUSTOM_SCHED_FILES = $(COMMON_FILES) tpool.c kstats.c ktimer.c 
THUMBNAIL_FILES = thumbnails.c
THUMBNAIL_LIBS = -fopenmp -lGraphicsMagick -larchive
THUMBNAIL_DIRS = -I/usr/include/GraphicsMagick

LINUX_NATIVE_FILES = linux.c native-timing.c $(THUMBNAIL_FILES) $(NON_CUSTOM_SCHED_FILES)
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
	gcc -std=gnu99 \
	    $(LINUX_NATIVE_FILES) \
	    $(THUMBNAIL_DIRS) \
	    $(THUMBNAIL_LIBS) \
	    -lpthread \
	    -o kweb

linux-upthread:
	gcc -g -std=gnu99 \
	    $(LINUX_UPTHREAD_FILES) \
	    -I/usr/include/upthread/compatibility \
	    $(THUMBNAIL_DIRS) \
	    $(THUMBNAIL_LIBS) \
	    -lupthread -lparlib \
	    -Wl,-wrap,socket \
	    -Wl,-wrap,accept \
	    -DWITH_PARLIB -DWITH_UPTHREAD \
	    -o kweb

linux-upthread-lithe:
	gcc -g -std=gnu99 \
	    $(LINUX_UPTHREAD_FILES) \
	    -I/usr/include/upthread-lithe/compatibility \
	    $(THUMBNAIL_DIRS) \
	    $(THUMBNAIL_LIBS) \
	    -lupthread-lithe -lithe -lparlib \
	    -Wl,-wrap,socket \
	    -Wl,-wrap,accept \
	    -DWITH_PARLIB -DWITH_UPTHREAD -DWITH_LITHE \
	    -o kweb

linux-custom-sched:
	gcc -g -std=gnu99
	    $(LINUX_CUSTOM_SCHED_FILES) \
	    -lparlib \
	    -Wl,-wrap,socket \
	    -Wl,-wrap,accept \
	    -DWITH_PARLIB -DWITH_CUSTOM_SCHED \
	    -o kweb

akaros:
	x86_64-ucb-akaros-gcc -std=gnu99 \
	     $(AKAROS_FILES) \
	    -lpthread -lbenchutil -lbsd
	    -o kweb

akaros-custom-sched:
	x86_64-ucb-akaros-gcc -std=gnu99 \
	    $(AKAROS_CUSTOM_SCHED_FILES) \
	    -lpthread -lbenchutil -lbsd \
	    -DWITH_CUSTOM_SCHED \
	    -o kweb

clean:
	rm -rf kweb

