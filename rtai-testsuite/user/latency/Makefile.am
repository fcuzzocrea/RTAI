testdir = $(prefix)/testsuite/user/latency

test_PROGRAMS = latency display

latency_SOURCES = latency.c

latency_CPPFLAGS = \
	@RTAI_USER_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../../rtai-core/include

latency_LDADD = \
	../../../rtai-core/sched/rtai/liblxrt/liblxrt.la \
	-lpthread

display_SOURCES = display.c

display_CPPFLAGS = \
	@RTAI_USER_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../../rtai-core/include

display_LDADD = \
	../../../rtai-core/sched/rtai/liblxrt/liblxrt.la \
	-lpthread

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(testdir)
	$(INSTALL_DATA) $(srcdir)/runinfo $(DESTDIR)$(testdir)/.runinfo
	@echo "\$${DESTDIR}$(prefix)/bin/rtai-load" > $(DESTDIR)$(testdir)/run
	@chmod +x $(DESTDIR)$(testdir)/run

run: all
	@$(top_srcdir)/rtai-core/scripts/rtai-load --verbose

EXTRA_DIST = runinfo
