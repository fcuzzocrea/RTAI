moduledir = $(DESTDIR)@RTAI_MODULE_DIR@

CROSS_COMPILE = @CROSS_COMPILE@

noinst_LIBRARIES = libhal.a

libhal_a_SOURCES = rtai.c

libhal_a_SOURCES += printk.c cswitch.S

if CONFIG_ARCH_SA1100
libhal_a_SOURCES += mach-sa1100/sa1100-timer.c
endif
if CONFIG_ARCH_PXA
libhal_a_SOURCES += mach-pxa/pxa-timer.c
endif
if CONFIG_ARCH_CLPS711X
libhal_a_SOURCES += mach-clps711x/clps711x-timer.c
endif

libhal_a_AR = $(CROSS_COMPILE)ar cru

INCLUDES = \
	@RTAI_KMOD_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../../include \
	-I../../../..

cswitch.o: CCASFLAGS += $(INCLUDES) -D__ASSEMBLY__

rtai_hal.o: libhal.a
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@

all-local: rtai_hal.o
if CONFIG_RTAI_OLD_FASHIONED_BUILD
	$(mkinstalldirs) $(top_srcdir)/modules
	$(INSTALL_DATA) $^ $(top_srcdir)/modules
endif

install-exec-local: rtai_hal.o
	$(mkinstalldirs) $(moduledir)
	$(INSTALL_DATA) $< $(moduledir)
