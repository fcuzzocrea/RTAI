moduledir = $(DESTDIR)@RTAI_MODULE_DIR@

CROSS_COMPILE = @CROSS_COMPILE@

noinst_LIBRARIES = libmodule.a

libmodule_a_SOURCES = \
			heap.c \
			intr.c \
			module.c \
			mutex.c \
			pod.c \
			shadow.c \
			synch.c \
			thread.c \
			timer.c

libmodule_a_CPPFLAGS = \
	@RTAI_KMOD_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../include

libmodule_a_AR = $(CROSS_COMPILE)ar cru

rtai_xeno.o: libmodule.a
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@

all-local: rtai_xeno.o
if CONFIG_RTAI_OLD_FASHIONED_BUILD
	$(mkinstalldirs) $(top_srcdir)/modules
	$(INSTALL_DATA) $^ $(top_srcdir)/modules
endif

install-exec-local: rtai_xeno.o
	$(mkinstalldirs) $(moduledir)
	$(INSTALL_DATA) $< $(moduledir)

OPTDIRS =

if CONFIG_RTAI_XENOMAI_VMLIB

lib_LIBRARIES = libxenomai.a

nodist_libxenomai_a_SOURCES = \
			heap.c \
			intr.c \
			module.c \
			mutex.c \
			pod.c \
			synch.c \
			thread.c \
			timer.c

libxenomai_a_CPPFLAGS = \
	@RTAI_USER_CFLAGS@ \
	-D_GNU_SOURCE -D_REENTRANT \
	-I$(srcdir)/.. \
	-I$(srcdir)/../../include/xenomai/vm \
	-I$(srcdir)/../../include \
	-I../../include \
	-I@RTAI_LINUX_DIR@/include

libxenomai_a_AR = $(CROSS_COMPILE)ar cru

endif

SUBDIRS = fusion $(OPTDIRS)
