includedir = $(prefix)/include/asm-cris

include_HEADERS = \
		rtai.h \
		rtai_atomic.h \
		rtai_lxrt.h \
		rtai_sched.h \
		rtai_shm.h \
		rtai_vectors.h

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(includedir)
	rm -f $(DESTDIR)$(includedir)/../asm
	$(LN_S) asm-cris $(DESTDIR)$(includedir)/../asm
