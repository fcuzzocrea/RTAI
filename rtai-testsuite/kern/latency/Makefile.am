testdir = $(prefix)/testsuite/kern/latency

CROSS_COMPILE = @CROSS_COMPILE@

noinst_LIBRARIES = librt_process.a

librt_process_a_SOURCES = rt_process.c

librt_process_a_AR = $(CROSS_COMPILE)ar cru

librt_process_a_CPPFLAGS = \
	@RTAI_KMOD_CFLAGS@ \
	@RTAI_FP_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../../rtai-core/include

test_PROGRAMS = display

display_SOURCES = display.c

display_CPPFLAGS = \
	@RTAI_USER_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../../rtai-core/include

rt_process.o: librt_process.a
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@

all-local: rt_process.o

install-exec-local: rt_process.o
	$(mkinstalldirs) $(DESTDIR)$(testdir)
	$(INSTALL_DATA) $^ $(DESTDIR)$(testdir)

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(testdir)
	$(INSTALL_DATA) $(srcdir)/runinfo $(DESTDIR)$(testdir)/.runinfo
	@echo "\$${DESTDIR}$(prefix)/bin/rtai-load" > $(DESTDIR)$(testdir)/run
	@chmod +x $(DESTDIR)$(testdir)/run

run: all
	@$(top_srcdir)/rtai-core/scripts/rtai-load --verbose

EXTRA_DIST = runinfo
