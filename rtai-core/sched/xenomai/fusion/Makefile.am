moduledir = $(DESTDIR)@RTAI_MODULE_DIR@

CROSS_COMPILE = @CROSS_COMPILE@

noinst_LIBRARIES = libmodule.a

lib_LIBRARIES = libfusion.a

libmodule_a_SOURCES = \
	dbridge.c \
	event.c \
	module.c \
	msg.c \
	sysent.c

libfusion_a_SOURCES = syslib.c

libmodule_a_AR = $(CROSS_COMPILE)ar cru

libfusion_a_AR = $(CROSS_COMPILE)ar cru

libmodule_a_CPPFLAGS = \
	@RTAI_KMOD_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../../include

rtai_fusion.o: libmodule.a
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@

libfusion_a_CPPFLAGS = \
	@RTAI_USER_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../../include

all-local: rtai_fusion.o
if CONFIG_RTAI_OLD_FASHIONED_BUILD
	$(mkinstalldirs) $(top_srcdir)/modules
	$(INSTALL_DATA) $^ $(top_srcdir)/modules
endif

install-exec-local: rtai_fusion.o
	$(mkinstalldirs) $(moduledir)
	$(INSTALL_DATA) $< $(moduledir)
