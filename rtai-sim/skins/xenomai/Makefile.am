vpath %.c @MVM_RTAIDIR@/rtai-core/sched/xenomai

CC = $(top_builddir)/gcic/gcic

override CFLAGS = -g

lib_LIBRARIES = libxenomai_s.a

# Shadow thread support makes no sense in native simulation
# mode. Therefore, shadow.c is not compiled.

nodist_libxenomai_s_a_SOURCES = \
			heap.c \
			intr.c \
			module.c \
			mutex.c \
			pod.c \
			synch.c \
			thread.c \
			timer.c

libxenomai_s_a_LIBADD = ../../adaptors/xenosa.o

INCLUDES = \
	--gcic-backend=$(prefix)/libexec/gcic \
	--kernel-code \
	-I$(top_srcdir)/include \
	-I@MVM_RTAIDIR@/rtai-core/include
