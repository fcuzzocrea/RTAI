includedir = $(prefix)/include/asm-i386

include_HEADERS = \
		rtai.h \
		rtai_atomic.h \
		rtai_fpu.h \
		rtai_hal.h \
		rtai_leds.h \
		rtai_legacy.h \
		rtai_lxrt.h \
		rtai_oldnames.h \
		rtai_sched.h \
		rtai_shm.h \
		rtai_srq.h \
		rtai_vectors.h \
		rtai_xeno.h \
		rtai_xncall.h \
		rtai_xnatomic.h

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(includedir)
	rm -f $(DESTDIR)$(includedir)/../asm
	$(LN_S) asm-i386 $(DESTDIR)$(includedir)/../asm
