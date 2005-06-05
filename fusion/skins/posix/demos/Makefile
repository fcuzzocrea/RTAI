prefix := $(shell rtai-config --prefix)

ifeq ($(prefix),)
$(error Please add <rtai-install-path>/bin to your PATH variable)
endif

CFLAGS_STD= -D_GNU_SOURCE -D_REENTRANT -Wstrict-prototypes -O2 -Wall
LDFLAGS_STD= -lpthread -lrt

CFLAGS_RT= $(shell rtai-config --posix-cflags)
LDFLAGS_RT= $(shell rtai-config --posix-ldflags)

all: accuracy accuracy_rt

accuracy: accuracy.c
	$(CC) $(CFLAGS_STD) $< -o $@ $(LDFLAGS_STD)

accuracy_rt: accuracy.c
	$(CC) $(CFLAGS_RT) $< -o $@ $(LDFLAGS_RT)

clean:
	rm -f accuracy accuracy_rt
