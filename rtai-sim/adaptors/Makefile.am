noinst_LIBRARIES = libxenosa.a librtaisa.a

libxenosa_a_SOURCES = xenosa.cc

librtaisa_a_SOURCES = rtaisa.cc

INCLUDES = \
	-D__MVM__ -D__MVMSA__ \
	@MVM_CXXFLAGS@ \
	-I$(srcdir)/../include \
	-I$(srcdir)/.. \
	-I@MVM_RTAIDIR@/rtai-core/include
