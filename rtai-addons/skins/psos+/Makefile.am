moduledir = $(DESTDIR)@RTAI_MODULE_DIR@

CROSS_COMPILE = @CROSS_COMPILE@

noinst_LIBRARIES = libmodule.a

libmodule_a_SOURCES = \
	module.c \
	asr.c \
	event.c \
	pt.c \
	queue.c \
	rn.c \
	task.c \
	tm.c \
	sem.c

libmodule_a_AR = $(CROSS_COMPILE)ar cru

noinst_HEADERS = \
	asr.h \
	defs.h \
	event.h \
	pt.h \
	queue.h \
	rn.h \
	task.h \
	tm.h \
	sem.h

include_HEADERS = \
	rtai_psos.h

libmodule_a_CPPFLAGS = \
	@RTAI_KMOD_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../../rtai-core/include \
	-I$(srcdir)/..

rtai_psos.o: libmodule.a
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@

all-local: rtai_psos.o
if CONFIG_RTAI_OLD_FASHIONED_BUILD
	$(mkinstalldirs) $(top_srcdir)/modules
	$(INSTALL_DATA) $^ $(top_srcdir)/modules
endif

install-exec-local: rtai_psos.o
	$(mkinstalldirs) $(moduledir)
	$(INSTALL_DATA) $< $(moduledir)

if CONFIG_RTAI_XENOMAI_VMLIB

lib_LIBRARIES = libpsos.a

nodist_libpsos_a_SOURCES = \
	module.c \
	asr.c \
	event.c \
	pt.c \
	queue.c \
	rn.c \
	task.c \
	tm.c \
	sem.c

libpsos_a_CPPFLAGS = \
	@RTAI_USER_CFLAGS@ \
	-D_GNU_SOURCE -D_REENTRANT \
	-I$(srcdir)/.. \
	-I$(top_srcdir)/rtai-core/include/xenomai/vm \
	-I$(top_srcdir)/rtai-core/include \
	-I../../../rtai-core/include \
	-I@RTAI_LINUX_DIR@/include

libpsos_a_AR = $(CROSS_COMPILE)ar cru

endif

EXTRA_DIST = doc
