vpath %.c @MVM_RTAIDIR@/rtai-addons/skins/vrtx

CC = $(top_builddir)/gcic/gcic

override CFLAGS = -g

INCLUDES = \
	--gcic-backend=$(prefix)/libexec/gcic \
	--skin-code \
	-I$(srcdir)/.. \
	-I$(top_srcdir)/include \
	-I@MVM_RTAIDIR@/rtai-addons/skins \
	-I@MVM_RTAIDIR@/rtai-core/include

lib_LIBRARIES = libvrtx_s.a

nodist_libvrtx_s_a_SOURCES = \
	event.c \
	heap.c \
	mb.c \
	module.c \
	mx.c \
	pt.c \
	queue.c \
	sem.c \
	task.c \
	time.c
