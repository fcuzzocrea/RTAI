moduledir = $(DESTDIR)@RTAI_MODULE_DIR@

CROSS_COMPILE = @CROSS_COMPILE@

noinst_LIBRARIES = libmodule.a       #Kernel

lib_LIBRARIES    = libkcomedilxrt.a  #USP

libmodule_a_SOURCES = \
		kcomedi_km.c 

libmodule_a_AR = $(CROSS_COMPILE)ar cru

libmodule_a_CFLAGS = \
	@RTAI_KMOD_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../rtai-core/include \
	-I@COMEDI_DIR@/include

libkcomedilxrt_a_CFLAGS = \
	@RTAI_USER_CFLAGS@ \
	-DCONFIG_RTAI_LXRT_INLINE=0 \
	-D_GNU_SOURCE -D_REENTRANT \
	-I$(top_srcdir)/rtai-core/include \
	-I../../rtai-core/include \
	-I@COMEDI_DIR@/include

libkcomedilxrt_a_SOURCES = \
		kcomedi_us.c 

libkcomedilxrt_a_AR = $(CROSS_COMPILE)ar cru

include_HEADERS = \
		rtai_comedi.h

# Build kernelmodule from archive
rtai_comedi.o: libmodule.a
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@

all-local: rtai_comedi.o
if CONFIG_RTAI_OLD_FASHIONED_BUILD
	$(mkinstalldirs) $(top_srcdir)/modules
	$(INSTALL_DATA) $^ $(top_srcdir)/modules
endif

install-exec-local: rtai_comedi.o
	$(mkinstalldirs) $(moduledir)
	$(INSTALL_DATA) $< $(moduledir)
