moduledir = $(DESTDIR)@RTAI_MODULE_DIR@

CROSS_COMPILE = @CROSS_COMPILE@

noinst_LIBRARIES = libmodule.a

libmodule_a_SOURCES = \
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

libmodule_a_AR = $(CROSS_COMPILE)ar cru

noinst_HEADERS = \
	cancel.h \
	cond.h \
	internal.h \
	mutex.h \
	sem.h \
	signal.h \
	thread.h \
	tsd.h

include_HEADERS = \
	rtai_pse51.h

libmodule_a_CPPFLAGS = \
	@RTAI_KMOD_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../../rtai-core/include \
	-I$(srcdir)/..

rtai_pse51.o: libmodule.a
	$(CROSS_COMPILE)$(LD) -r -o $@ --whole-archive $^

all-local: rtai_pse51.o
if CONFIG_RTAI_OLD_FASHIONED_BUILD
	$(mkinstalldirs) $(top_srcdir)/modules
	$(INSTALL_DATA) $^ $(top_srcdir)/modules
endif

install-exec-local: rtai_pse51.o
	$(mkinstalldirs) $(moduledir)
	$(INSTALL_DATA) $< $(moduledir)

EXTRA_DIST = doc
