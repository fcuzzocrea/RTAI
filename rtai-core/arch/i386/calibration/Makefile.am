calibrationdir = $(prefix)/calibration

CROSS_COMPILE = @CROSS_COMPILE@

noinst_LIBRARIES = librt_process.a

librt_process_a_SOURCES = rt_process.c calibrate.h

librt_process_a_AR = $(CROSS_COMPILE)ar cru

librt_process_a_CPPFLAGS = \
	@RTAI_KMOD_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../../include

calibration_PROGRAMS = calibrate

if CONFIG_RTAI_SCHED_LXRT
calibration_PROGRAMS += _calibrate
endif

calibrate_SOURCES = calibrate.c calibrate.h

calibrate_CPPFLAGS = \
	@RTAI_USER_CFLAGS@ \
	-DKERNEL_HELPER_PATH="\"$(calibrationdir)/rt_process.o\"" \
	-DUSER_HELPER_PATH="\"$(calibrationdir)/_calibrate\"" \
	-I$(top_srcdir)/rtai-core/include \
	-I../../../include

# calibrate_LDADD = \
# 	../../../sched/rtai/liblxrt/liblxrt.la \
# 	-lpthread

if CONFIG_RTAI_SCHED_LXRT
_calibrate_SOURCES = _calibrate.c

_calibrate_CPPFLAGS = \
	@RTAI_USER_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../../include

_calibrate_LDADD = \
	../../../sched/rtai/liblxrt/liblxrt.la \
	-lpthread
endif

rt_process.o: librt_process.a
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@

all-local: rt_process.o

install-exec-local: rt_process.o
	$(mkinstalldirs) $(DESTDIR)$(calibrationdir)
	$(INSTALL_DATA) $^ $(DESTDIR)$(calibrationdir)

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(calibrationdir)
	$(INSTALL_DATA) $(srcdir)/runinfo $(DESTDIR)$(calibrationdir)/.runinfo
	@echo "\$${DESTDIR}$(prefix)/bin/rtai-load" > $(DESTDIR)$(calibrationdir)/run
	@chmod +x $(DESTDIR)$(calibrationdir)/run

run: all
	@$(top_srcdir)/rtai-scripts/rtai-load --verbose

EXTRA_DIST = runinfo README.SMI
