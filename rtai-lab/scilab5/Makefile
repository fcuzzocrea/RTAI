# Copyright (C) 2017 The RTAI project
# This [file] is free software; the RTAI project
# gives unlimited permission to copy and/or distribute it,
# with or without modifications, as long as this notice is preserved.
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY, to the extent permitted by law; without
# even the implied warranty of MERCHANTABILITY or FITNESS FOR A
# PARTICULAR PURPOSE.

all: 

SCILAB_VERSION = 5.5.2
SCILAB_DIR = /usr/local/scilab-$(SCILAB_VERSION)/share/scilab
SCILAB=scilab5

RTAIDIR = $(shell rtai-config --prefix)

CONTRIBDIR = $(SCILAB_DIR)/contrib/RTAI
LIBDIR     =  $(SCILAB_DIR)/

RTAISRC  = builder.sce loader.sce
RTAISUBDIR = etc macros RT_templates src

install: install_static

install_static:
	mkdir -p $(CONTRIBDIR)
	cd RTAI; cp $(RTAISRC) $(CONTRIBDIR)
	cd RTAI; cp -dpR $(RTAISUBDIR) $(CONTRIBDIR)
	cd $(CONTRIBDIR)/RT_templates; rm rtai_sh.mak;
	cd $(CONTRIBDIR); $(SCILAB) -nw -f builder.sce; exit
	cd scicos_src; make; make install
	mkdir -p $(SCILAB_DIR)/libs; cp -dpR libs/*.a $(SCILAB_DIR)/libs
	cd devices; make; make install
	cp devices/machine.h $(SCILAB_DIR)/../../include/scilab/scicos

install_shared:
	mkdir -p $(CONTRIBDIR)
	cd RTAI; cp $(RTAISRC) $(CONTRIBDIR)
	cd RTAI; cp -dpR $(RTAISUBDIR) $(CONTRIBDIR)
	cd $(CONTRIBDIR)/RT_templates; rm rtai.mak; mv rtai_sh.mak rtai.mak;
	cd $(CONTRIBDIR); $(SCILAB) -nw -f builder.sce; exit
	cd devices; make; make install
	cp devices/machine.h $(SCILAB_DIR)/../../include/scilab/scicos

user:
	mkdir -p ~/.Scilab/scilab-$(SCILAB_VERSION)
	cat scilab >> ~/.Scilab/scilab-$(SCILAB_VERSION)/.scilab
