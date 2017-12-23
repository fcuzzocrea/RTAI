# Copyright (C) 2005-2017 The RTAI project
# This [file] is free software; the RTAI project
# gives unlimited permission to copy and/or distribute it,
# with or without modifications, as long as this notice is preserved.
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY, to the extent permitted by law; without
# even the implied warranty of MERCHANTABILITY or FITNESS FOR A
# PARTICULAR PURPOSE.

LIB = libsciblk.a
all: $(LIB)

RTAIDIR = $(shell rtai-config --prefix)
COMEDIDIR = $(shell rtai-config --comedi-dir)

SRCALL = $(wildcard *.c)

ifeq ($(strip $(COMEDIDIR)),)
EXCLUDE = exit_on_error.c rtai_comedi_datain.c rtai_comedi_dataout.c rtai_comedi_dioin.c rtai_comedi_dioout.c rtai_comedi_encoder.c get_softcalib.c
else
EXCLUDE = exit_on_error.c
endif

SRC=$(filter-out $(EXCLUDE),$(SRCALL))

OBJ = $(SRC:.c=.o)
DBG = -g

CC_FLAGS = -c $(DBG) -I. -I$(RTAIDIR)/include -O2 -Wall -std=gnu99 -DSOFTCALIB

%.o: %.c
	cc $(CC_FLAGS) $<

$(LIB): $(OBJ)
	ar -r $(LIB) $(OBJ)

install: $(LIB)
	cp $(LIB) $(RTAIDIR)/lib

clean:
	rm -f $(LIB) $(OBJ)
