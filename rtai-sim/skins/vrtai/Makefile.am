CC = $(top_builddir)/gcic/gcic

override CFLAGS = -g

lib_LIBRARIES = librtai_s.a

librtai_s_a_SOURCES = \
	rt_mem_mgr.c \
	rtai_sched.c

librtai_s_a_LIBADD = ../../adaptors/rtaisa.o

librtai_s_a_CPPFLAGS = \
	--gcic-backend=$(prefix)/libexec/gcic \
	--skin-code \
	-DCONFIG_RTAI_DYN_MM \
	-DCONFIG_RTAI_FPU_SUPPORT \
	-I$(top_srcdir)/include
