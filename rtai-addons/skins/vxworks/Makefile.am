moduledir = $(DESTDIR)@RTAI_MODULE_DIR@

CROSS_COMPILE = @CROSS_COMPILE@

noinst_LIBRARIES = libmodule.a

libmodule_a_SOURCES = \
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

libmodule_a_AR = $(CROSS_COMPILE)ar cru

noinst_HEADERS = \
	defs.h

include_HEADERS = \
	rtai_vxworks.h

libmodule_a_CPPFLAGS = \
	@RTAI_KMOD_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../../rtai-core/include \
	-I$(srcdir)/..

rtai_vxworks.o: libmodule.a
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@

all-local: rtai_vxworks.o
if CONFIG_RTAI_OLD_FASHIONED_BUILD
	$(mkinstalldirs) $(top_srcdir)/modules
	$(INSTALL_DATA) $^ $(top_srcdir)/modules
endif

install-exec-local: rtai_vxworks.o
	$(mkinstalldirs) $(moduledir)
	$(INSTALL_DATA) $< $(moduledir)

if CONFIG_RTAI_XENOMAI_VMLIB

lib_LIBRARIES = libvxworks.a

nodist_libvxworks_a_SOURCES = \
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

libvxworks_a_CPPFLAGS = \
	@RTAI_USER_CFLAGS@ \
	-D_GNU_SOURCE -D_REENTRANT \
	-I$(srcdir)/.. \
	-I$(top_srcdir)/rtai-core/include/xenomai/vm \
	-I$(top_srcdir)/rtai-core/include \
	-I../../../rtai-core/include \
	-I@RTAI_LINUX_DIR@/include

libvxworks_a_AR = $(CROSS_COMPILE)ar cru

endif

EXTRA_DIST = doc
