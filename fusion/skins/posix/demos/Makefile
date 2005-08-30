RTAI_CONFIG=rtai-config
prefix := $(shell $(RTAI_CONFIG) --prefix)

ifeq ($(prefix),)
$(error Please add <rtai-install-path>/bin to your PATH variable)
endif

CC:= $(shell $(RTAI_CONFIG) --cc)
CFLAGS_STD= -D_GNU_SOURCE -D_REENTRANT -Wstrict-prototypes -O2 -Wall -DSPERIOD=2000
LDFLAGS_STD= -lpthread -lrt

CFLAGS_RT:= $(shell $(RTAI_CONFIG) --posix-cflags) -DSPERIOD=100
LDFLAGS_RT:= $(shell $(RTAI_CONFIG) --posix-ldflags)

# Using a single source image, this demo illustrates the real-time
# guarantees provided by fusion's POSIX skin in user-space
# (accuracy_rt) compared to those available from the native
# NPTL/LinuxThreads supports (accuracy). You need the rtai_hal.ko +
# rtai_nucleus.ko + rtai_posix.ko modules to be loaded to run the
# accuracy_rt test.

EXES = accuracy accuracy_rt satch_rt

all: $(EXES)

accuracy: accuracy.c
	$(CC) $(CFLAGS_STD) $< -o $@ $(LDFLAGS_STD)

accuracy_rt: accuracy.c
	$(CC) $(CFLAGS_RT) $< -o $@ $(LDFLAGS_RT)

satch_rt: satch.c
	$(CC) $(CFLAGS_RT) $< -o $@ $(LDFLAGS_RT)

clean:
	$(RM) $(EXES)
