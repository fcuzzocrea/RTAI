vpath %.c @MVM_RTAIDIR@/rtai-addons/skins/psos+

CC = $(top_builddir)/gcic/gcic

override CFLAGS = -g

INCLUDES = \
	--gcic-backend=$(prefix)/libexec/gcic \
	--skin-code \
	-I$(srcdir)/.. \
	-I$(top_srcdir)/include \
	-I@MVM_RTAIDIR@/rtai-addons/skins \
	-I@MVM_RTAIDIR@/rtai-core/include

lib_LIBRARIES = libpsos_s.a

nodist_libpsos_s_a_SOURCES = \
	module.c \
	asr.c \
	event.c \
	pt.c \
	queue.c \
	rn.c \
	task.c \
	tm.c \
	sem.c \
	vmctl.c

install-data-local:
	$(mkinstalldirs) $(pkgdatadir)/tcl/psos
	$(INSTALL_DATA) $(srcdir)/*.tcl $(pkgdatadir)/tcl/psos
	echo "auto_mkindex $(pkgdatadir)/tcl/psos" | @TCL_SCRIPT@

EXTRA_DIST = queue.tcl task.tcl vmctl.c
