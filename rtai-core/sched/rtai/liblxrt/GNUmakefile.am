lib_LTLIBRARIES = liblxrt.la

liblxrt_la_SOURCES = \
		init.c \
		services.c \
		touchall.c

liblxrt_la_LDFLAGS = -module -version-info 1:0:0

INCLUDES = \
	@RTAI_USER_CFLAGS@ \
	-D_GNU_SOURCE \
	-fno-inline \
	-I$(top_srcdir)/rtai-core/include \
	-I../../../include

if CONFIG_RTAI_SERIAL
INCLUDES += -I$(top_srcdir)/rtai-addons/serial
endif
