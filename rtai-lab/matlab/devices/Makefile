SRC= \
sfun_comedi_data_read.c \
sfun_comedi_data_write.c \
sfun_comedi_dio_read.c \
sfun_comedi_dio_write.c \
sfun_rtai_led.c \
sfun_rtai_log.c \
sfun_rtai_meter.c \
sfun_rtai_scope.c \
sfun_rtai_synchronoscope.c

MEX = /usr/local/matlab6p5/bin/mex
MEX_FLAGS= -v

all: $(SRC:.c=.mexglx)

%.mexglx: %.c
	$(MEX) $(MEX_FLAGS) $<

clean:
	rm -f *.mexglx

install:

%::
	true
