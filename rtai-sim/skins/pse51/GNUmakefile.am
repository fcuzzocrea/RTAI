vpath %.c @MVM_RTAIDIR@/rtai-addons/skins/pse51

includedir = $(prefix)/include/sim

CC = $(top_builddir)/gcic/gcic

override CFLAGS = -g

INCLUDES = \
	--gcic-backend=$(prefix)/libexec/gcic \
	--skin-code \
	-I$(srcdir)/.. \
	-I$(top_srcdir)/include \
	-I@MVM_RTAIDIR@/rtai-addons/skins \
	-I@MVM_RTAIDIR@/rtai-core/include

lib_LIBRARIES = libpse51_s.a

nodist_libpse51_s_a_SOURCES = \
	module.c \
	sched.c \
	thread_attr.c \
	thread.c \
	mutex_attr.c \
	mutex.c \
	cond_attr.c \
	cond.c \
	sem.c \
	cancel.c \
	once.c \
	signal.c \
	tsd.c \
	clock.c 

include_HEADERS = \
	pse51_overrides.h

SUBDIRS = . testsuite
