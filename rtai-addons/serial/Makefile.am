moduledir = $(DESTDIR)@RTAI_MODULE_DIR@

CROSS_COMPILE = @CROSS_COMPILE@

noinst_LIBRARIES = libserial.a

libserial_a_SOURCES = \
		serial.c \
		serialP.h

include_HEADERS = rtai_serial.h

libserial_a_AR = $(CROSS_COMPILE)ar cru

INCLUDES = \
	@RTAI_KMOD_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../rtai-core/include

rtai_serial.o: libserial.a
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@

all-local: rtai_serial.o
if CONFIG_RTAI_OLD_FASHIONED_BUILD
	$(mkinstalldirs) $(top_srcdir)/modules
	$(INSTALL_DATA) $^ $(top_srcdir)/modules
endif

install-exec-local: rtai_serial.o
	$(mkinstalldirs) $(moduledir)
	$(INSTALL_DATA) $< $(moduledir)
