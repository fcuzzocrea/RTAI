CC = ../../../gcic/gcic

INCLUDES = \
	--gcic-backend=$(prefix)/libexec/gcic \
	-I$(srcdir)/.. \
	-I@MVM_RTAIDIR@/rtai-addons/skins \
	-I@MVM_RTAIDIR@/rtai-addons/skins/pse51 \
	-I@MVM_RTAIDIR@/rtai-core/include \
	-I$(top_srcdir)/include

LDFLAGS = \
	--gcic-backend=$(prefix)/libexec/gcic \
	--cc=$(CXX) \
	--no-mvm-libs \
	../libpse51_s.a \
	../../xenomai/libxenomai_s.a \
	libtests.a \
	../../../vm/libmvm.la \
	../../../vmutils/libmvmutils.la \
	../../../adaptors/libxenosa.a

check_PROGRAMS= \
	threads \
	semaphores \
	mutexes \
	condvars \
	cancellation \
	signals \
	tsd \
	clock

TESTS= $(check_PROGRAMS)

threads_DEPENDENCIES= ../libpse51_s.a libtests.a
semaphores_DEPENDENCIES= ../libpse51_s.a libtests.a
mutexes_DEPENDENCIES=../libpse51_s.a libtests.a
condvars_DEPENDENCIES=../libpse51_s.a libtests.a
cancellation_DEPENDENCIES=../libpse51_s.a libtests.a
signals_DEPENDENCIES=../libpse51_s.a libtests.a
tsd_DEPENDENCIES=../libpse51_s.a libtests.a
clock_DEPENDENCIES=../libpse51_s.a libtests.a

check_LIBRARIES=libtests.a

libtests_a_SOURCES = xntest.c xntest.h pse51_test.c pse51_test.h
threads_SOURCES = threads.c
semaphores_SOURCES = semaphores.c
mutexes_SOURCES = mutexes.c
condvars_SOURCES = condvars.c
cancellation_SOURCES = cancellation.c
signals_SOURCES = signals.c
tsd_SOURCES = tsd.c
clock_SOURCES = clock.c

EXTRA_DIST = skeleton.c
