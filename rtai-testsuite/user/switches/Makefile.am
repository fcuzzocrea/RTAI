testdir = $(prefix)/testsuite/user/switches

test_PROGRAMS = switches

switches_SOURCES = switches.c

switches_CPPFLAGS = \
	@RTAI_USER_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../../rtai-core/include

switches_LDADD = \
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
