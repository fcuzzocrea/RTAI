vpath %.c @MVM_RTAIDIR@/rtai-addons/skins/vxworks

CC = $(top_builddir)/gcic/gcic

override CFLAGS = -g

INCLUDES = \
	--gcic-backend=$(prefix)/libexec/gcic \
	--skin-code \
	-I$(srcdir)/.. \
	-I$(top_srcdir)/include \
	-I@MVM_RTAIDIR@/rtai-addons/skins \
	-I@MVM_RTAIDIR@/rtai-core/include

lib_LIBRARIES = libvxworks_s.a

nodist_libvxworks_s_a_SOURCES = \
	errnoLib.c \
	intLib.c \
	kernelLib.c \
	module.c \
	msgQLib.c \
	semLib.c \
	sysLib.c \
	taskHookLib.c \
	taskInfo.c \
	taskLib.c \
	tickLib.c \
	wdLib.c
