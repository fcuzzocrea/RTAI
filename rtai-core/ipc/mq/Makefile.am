moduledir = $(DESTDIR)@RTAI_MODULE_DIR@

CROSS_COMPILE = @CROSS_COMPILE@

noinst_LIBRARIES = libmq.a

libmq_a_SOURCES = mq.c

libmq_a_AR = $(CROSS_COMPILE)ar cru

INCLUDES = \
	@RTAI_KMOD_CFLAGS@ \
	-I$(top_srcdir)/rtai-core/include \
	-I../../include

rtai_mq.o: libmq.a
	$(CROSS_COMPILE)$(LD) --whole-archive $< -r -o $@

all-local: rtai_mq.o
if !CONFIG_RTAI_MQ_BUILTIN
if CONFIG_RTAI_OLD_FASHIONED_BUILD
	$(mkinstalldirs) $(top_srcdir)/modules
	$(INSTALL_DATA) $^ $(top_srcdir)/modules
endif

install-exec-local: rtai_mq.o
	$(mkinstalldirs) $(moduledir)
	$(INSTALL_DATA) $< $(moduledir)
endif
