includedir = $(prefix)/include/asm-arm

nobase_include_HEADERS = \
		arch-clps711x/rtai_arch.h \
		arch-clps711x/rtai_exports.h \
		arch-clps711x/rtai_timer.h \
		arch-pxa/rtai_arch.h \
		arch-pxa/rtai_exports.h \
		arch-pxa/rtai_timer.h \
		arch-sa1100/rtai_arch.h \
		arch-sa1100/rtai_exports.h \
		arch-sa1100/rtai_timer.h \
		rtai.h \
		rtai_atomic.h \
		rtai_debug.h \
		rtai_fpu.h \
		rtai_irqops.h \
		rtai_lxrt.h \
		rtai_sched.h \
		rtai_shm.h \
		rtai_srq.h \
		rtai_vectors.h

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(includedir)
	$(LN_S) -f asm-arm $(DESTDIR)$(includedir)/../asm
	$(LN_S) -f arch-@RTAI_TARGET_SUBARCH@ $(DESTDIR)$(includedir)/arch
