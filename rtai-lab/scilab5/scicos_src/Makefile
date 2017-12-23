# Copyright (C) 2017 The RTAI project
# This [file] is free software; the RTAI project
# gives unlimited permission to copy and/or distribute it,
# with or without modifications, as long as this notice is preserved.
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY, to the extent permitted by law; without
# even the implied warranty of MERCHANTABILITY or FITNESS FOR A
# PARTICULAR PURPOSE.

LIB = libscicos.a
all: $(LIB)


SCIDIR = ../

SRC_C = $(wildcard *.c) 
SRC_F = $(wildcard *.f) 

OBJ_C = $(SRC_C:.c=.o)
OBJ_F = $(SRC_F:.f=.o)
DBG =

INC = -I$(SCIDIR)/include/scilab 

CC_FLAGS = -c $(DBG) $(INC) -I./include -O2

%.o: %.c
	cc $(CC_FLAGS) $<

%.o: %.f
	cc $(CC_FLAGS) $<

$(LIB): $(OBJ_C) $(OBJ_F)
	ar -r $(LIB) $(OBJ_C) $(OBJ_F)

install:
	cp $(LIB) ../libs/

clean:
	rm -f $(LIB) $(OBJ_C) $(OBJ_F)
