moduledir = $(DESTDIR)@RTAI_MODULE_DIR@

CROSS_COMPILE = @CROSS_COMPILE@

noinst_LIBRARIES = libleds.a

libleds_a_SOURCES = leds.c

libleds_a_AR = $(CROSS_COMPILE)ar cru

INCLUDES = \
	@RTAI_KMOD_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../include

rtai_leds.o: libleds.a
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@

all-local: rtai_leds.o
if !CONFIG_RTAI_LEDS_BUILTIN
if CONFIG_RTAI_OLD_FASHIONED_BUILD
	$(mkinstalldirs) $(top_srcdir)/modules
	$(INSTALL_DATA) $^ $(top_srcdir)/modules
endif

install-exec-local: rtai_leds.o
	$(mkinstalldirs) $(moduledir)
	$(INSTALL_DATA) $< $(moduledir)
endif
