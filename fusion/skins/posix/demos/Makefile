prefix := $(shell rtai-config --prefix)

ifeq ($(prefix),)
$(error Please add <rtai-install-path>/bin to your PATH variable)
endif

CFLAGS_STD= -D_GNU_SOURCE -D_REENTRANT -Wstrict-prototypes -O2 -Wall
LDFLAGS_STD= -lpthread -lrt

CFLAGS_RT= $(shell rtai-config --posix-cflags)
LDFLAGS_RT= $(shell rtai-config --posix-ldflags)

# Using a single source image, this demo illustrates the real-time
# guarantees provided by fusion's POSIX skin in user-space
# (accuracy_rt) compared to those available from the native
# NPTL/LinuxThreads supports (accuracy). You need the rtai_hal.ko +
# rtai_nucleus.ko + rtai_posix.ko modules loaded to run the
# accuracy_rt test.

all: accuracy accuracy_rt

accuracy: accuracy.c
	$(CC) $(CFLAGS_STD) $< -o $@ $(LDFLAGS_STD)

accuracy_rt: accuracy.c
	$(CC) $(CFLAGS_RT) $< -o $@ $(LDFLAGS_RT)

clean:
	rm -f accuracy accuracy_rt
