includedir = $(prefix)/include/asm-mips

include_HEADERS = \
		rtai.h \
		rtai_atomic.h \
		rtai_lxrt.h \
		rtai_sched.h \
		rtai_shm.h \
		rtai_srq.h \
		rtai_stackframe.h \
		rtai_vectors.h

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(includedir)
	rm -f $(DESTDIR)$(includedir)/../asm
	$(LN_S) asm-mips $(DESTDIR)$(includedir)/../asm
