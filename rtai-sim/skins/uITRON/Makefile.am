vpath %.c @MVM_RTAIDIR@/rtai-addons/skins/uITRON

CC = $(top_builddir)/gcic/gcic

override CFLAGS = -g

INCLUDES = \
	--gcic-backend=$(prefix)/libexec/gcic \
	--skin-code \
	-I$(srcdir)/.. \
	-I$(top_srcdir)/include \
	-I@MVM_RTAIDIR@/rtai-addons/skins \
	-I@MVM_RTAIDIR@/rtai-core/include

lib_LIBRARIES = libuitron_s.a

nodist_libuitron_s_a_SOURCES = \
	flag.c \
	mbx.c \
	module.c \
	sem.c \
	task.c
