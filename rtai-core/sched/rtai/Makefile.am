moduledir = $(DESTDIR)@RTAI_MODULE_DIR@
schedulers =
noinst_LIBRARIES =

CROSS_COMPILE = @CROSS_COMPILE@

builtins = @RTAI_BUILTIN_MODLIST@
BUILTIN_OBJS = $(foreach obj,$(builtins),$(patsubst %,../../%/rtai_$(notdir $(obj)).o,$(obj)))

if CONFIG_SMP
default_ksched = rtai_smp.o
else
default_ksched = rtai_up.o
endif

if CONFIG_RTAI_SCHED_UP
noinst_LIBRARIES += libsched_up.a

libsched_up_a_SOURCES = sched_up.c common.c

libsched_up_a_AR = $(CROSS_COMPILE)ar cru

rtai_up.o: libsched_up.a $(BUILTIN_OBJS)
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@ $(BUILTIN_OBJS)

libsched_up_a_CPPFLAGS = \
	@RTAI_KMOD_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../include

schedulers += rtai_up.o
endif

if CONFIG_RTAI_SCHED_MUP
noinst_LIBRARIES += libsched_mup.a

libsched_mup_a_SOURCES = sched_mup.c common.c

libsched_mup_a_AR = $(CROSS_COMPILE)ar cru

rtai_mup.o: libsched_mup.a $(BUILTIN_OBJS)
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@ $(BUILTIN_OBJS)

libsched_mup_a_CPPFLAGS = \
	@RTAI_KMOD_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../include

schedulers += rtai_mup.o

if CONFIG_SMP
if CONFIG_X86
libsched_mup_a_CPPFLAGS += -D__USE_APIC__

noinst_LIBRARIES += libsched_mup-noapic.a

libsched_mup_noapic_a_SOURCES = sched_mup.c common.c

libsched_mup_noapic_a_AR = $(CROSS_COMPILE)ar cru

rtai_mup-noapic.o: libsched_mup-noapic.a $(BUILTIN_OBJS)
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@ $(BUILTIN_OBJS)

libsched_mup_noapic_a_CPPFLAGS = \
	@RTAI_KMOD_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../include

schedulers += rtai_mup-noapic.o
endif
endif

endif

if CONFIG_RTAI_SCHED_SMP
noinst_LIBRARIES += libsched_smp.a

libsched_smp_a_SOURCES = sched_smp.c common.c

libsched_smp_a_AR = $(CROSS_COMPILE)ar cru

rtai_smp.o: libsched_smp.a $(BUILTIN_OBJS)
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@ $(BUILTIN_OBJS)

libsched_smp_a_CPPFLAGS = \
	@RTAI_KMOD_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../include

schedulers += rtai_smp.o

if CONFIG_SMP
if CONFIG_X86
libsched_smp_a_CPPFLAGS += -D__USE_APIC__

noinst_LIBRARIES += libsched_smp-noapic.a

libsched_smp_noapic_a_SOURCES = sched_smp.c common.c

libsched_smp_noapic_a_AR = $(CROSS_COMPILE)ar cru

rtai_smp-noapic.o: libsched_smp-noapic.a $(BUILTIN_OBJS)
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@ $(BUILTIN_OBJS)

libsched_smp_noapic_a_CPPFLAGS = \
	@RTAI_KMOD_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../include

schedulers += rtai_smp-noapic.o
endif
endif

endif

if CONFIG_RTAI_SCHED_LXRT
noinst_LIBRARIES += libsched_lxrt.a

libsched_lxrt_a_SOURCES = sched_lxrt.c sys_lxrt.c common.c

libsched_lxrt_a_AR = $(CROSS_COMPILE)ar cru

rtai_lxrt.o: libsched_lxrt.a $(BUILTIN_OBJS)
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@ $(BUILTIN_OBJS)

libsched_lxrt_a_CPPFLAGS = \
	@RTAI_KMOD_CFLAGS@ \
	-D__RTAI_LXRT__ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../include

schedulers += rtai_lxrt.o

if CONFIG_SMP
if CONFIG_X86
libsched_lxrt_a_CPPFLAGS += -D__USE_APIC__

noinst_LIBRARIES += libsched_lxrt-noapic.a

libsched_lxrt_noapic_a_SOURCES = sched_lxrt.c sys_lxrt.c common.c

libsched_lxrt_noapic_a_AR = $(CROSS_COMPILE)ar cru

rtai_lxrt-noapic.o: libsched_lxrt-noapic.a $(BUILTIN_OBJS)
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@ $(BUILTIN_OBJS)

libsched_lxrt_noapic_a_CPPFLAGS = \
	@RTAI_KMOD_CFLAGS@ \
	-D__RTAI_LXRT__ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../include

schedulers += rtai_lxrt-noapic.o
endif
endif

endif

all-local: $(schedulers)
if CONFIG_RTAI_OLD_FASHIONED_BUILD
	$(mkinstalldirs) $(top_srcdir)/modules
	$(INSTALL_DATA) $^ $(top_srcdir)/modules
	rm -f $(top_srcdir)/modules/rtai_ksched.o
	$(LN_S) $(default_ksched) $(top_srcdir)/modules/rtai_ksched.o
endif

install-exec-local: $(schedulers)
	$(mkinstalldirs) $(moduledir)
	$(INSTALL_DATA) $(schedulers) $(moduledir)
	rm -f $(moduledir)/rtai_ksched.o
	$(LN_S) $(default_ksched) $(moduledir)/rtai_ksched.o

if CONFIG_RTAI_SCHED_LXRT
OPTDIRS = liblxrt
endif

SUBDIRS = $(OPTDIRS)
