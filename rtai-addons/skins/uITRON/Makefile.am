moduledir = $(DESTDIR)@RTAI_MODULE_DIR@

CROSS_COMPILE = @CROSS_COMPILE@

noinst_LIBRARIES = libmodule.a

libmodule_a_SOURCES = \
	flag.c \
	mbx.c \
	module.c \
	sem.c \
	task.c

libmodule_a_AR = $(CROSS_COMPILE)ar cru

noinst_HEADERS = \
	defs.h \
	flag.h \
	mbx.h \
	sem.h \
	task.h

include_HEADERS = \
	rtai_uITRON.h

libmodule_a_CPPFLAGS = \
	@RTAI_KMOD_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../../rtai-core/include \
	-I$(srcdir)/..

rtai_uITRON.o: libmodule.a
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@

all-local: rtai_uITRON.o
if CONFIG_RTAI_OLD_FASHIONED_BUILD
	$(mkinstalldirs) $(top_srcdir)/modules
	$(INSTALL_DATA) $^ $(top_srcdir)/modules
endif

install-exec-local: rtai_uITRON.o
	$(mkinstalldirs) $(moduledir)
	$(INSTALL_DATA) $< $(moduledir)

if CONFIG_RTAI_XENOMAI_VMLIB

lib_LIBRARIES = libuitron.a

nodist_libuitron_a_SOURCES = \
	flag.c \
	mbx.c \
	module.c \
	sem.c \
	task.c

libuitron_a_CPPFLAGS = \
	@RTAI_USER_CFLAGS@ \
	-D_GNU_SOURCE -D_REENTRANT \
	-I$(srcdir)/.. \
	-I$(top_srcdir)/rtai-core/include/xenomai/vm \
	-I$(top_srcdir)/rtai-core/include \
	-I../../../rtai-core/include \
	-I@RTAI_LINUX_DIR@/include

libuitron_a_AR = $(CROSS_COMPILE)ar cru

endif

EXTRA_DIST = doc
