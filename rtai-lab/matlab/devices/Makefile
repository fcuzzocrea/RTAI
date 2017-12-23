# Copyright (C) 2005-2017 The RTAI project
# This [file] is free software; the RTAI project
# gives unlimited permission to copy and/or distribute it,
# with or without modifications, as long as this notice is preserved.
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY, to the extent permitted by law; without
# even the implied warranty of MERCHANTABILITY or FITNESS FOR A
# PARTICULAR PURPOSE.

SRC := $(wildcard *.c)

MEX = /usr/local/matlab7/bin/mex
MEX_FLAGS= -v

all: $(SRC:.c=.mexglx)

%.mexglx: %.c
	$(MEX) $(MEX_FLAGS) $<

clean:
	rm -f *.mexglx

install:

%::
	true
