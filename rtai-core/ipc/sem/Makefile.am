moduledir = $(DESTDIR)@RTAI_MODULE_DIR@

CROSS_COMPILE = @CROSS_COMPILE@

noinst_LIBRARIES = libsem.a

libsem_a_SOURCES = sem.c

libsem_a_AR = $(CROSS_COMPILE)ar cru

INCLUDES = \
	@RTAI_KMOD_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../include

rtai_sem.o: libsem.a
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@

all-local: rtai_sem.o
if !CONFIG_RTAI_SEM_BUILTIN
if CONFIG_RTAI_OLD_FASHIONED_BUILD
	$(mkinstalldirs) $(top_srcdir)/modules
	$(INSTALL_DATA) $^ $(top_srcdir)/modules
endif

install-exec-local: rtai_sem.o
	$(mkinstalldirs) $(moduledir)
	$(INSTALL_DATA) $< $(moduledir)
endif
