demodir = $(prefix)/demos/rtai-sim/psos+/satch

CC = ../../../gcic/gcic

override CFLAGS = -g

demo_PROGRAMS = satch_s

satch_s_SOURCES = satch.c

satch_s_CPPFLAGS = \
	--gcic-backend=$(prefix)/libexec/gcic \
	-I$(top_srcdir)/include \
	-I@MVM_RTAIDIR@/rtai-addons/skins/psos+ \
	-I@MVM_RTAIDIR@/rtai-core/include

satch_s_LDFLAGS = \
	--gcic-backend=$(prefix)/libexec/gcic \
	--cc=$(CXX) \
	--no-mvm-libs \
	../../../skins/psos+/libpsos_s.a \
	../../../skins/xenomai/libxenomai_s.a \
	../../../vm/libmvm.la \
	../../../vmutils/libmvmutils.la \
	../../../adaptors/libxenosa.a
