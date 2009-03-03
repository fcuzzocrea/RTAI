all: install

SCILAB_VERSION = 5.1
SCILAB_DIR = /usr/local/scilab-$(SCILAB_VERSION)/share/scilab

RTAIDIR = $(shell rtai-config --prefix)

CONTRIBDIR = $(SCILAB_DIR)/contrib/RTAI
LIBDIR     =  $(SCILAB_DIR)/

RTAISRC  = builder.sce loader.sce
RTAISUBDIR = etc macros RT_templates src

install:
	mkdir -p $(CONTRIBDIR)
	cd RTAI; cp $(RTAISRC) $(CONTRIBDIR)
	cd RTAI; cp -dpR $(RTAISUBDIR) $(CONTRIBDIR)
	cd $(CONTRIBDIR); scilab -nw -f builder.sce; exit
	cp -dpR libs $(SCILAB_DIR)
	cd devices; make; make install

user:
	mkdir -p ~/.Scilab/scilab-$(SCILAB_VERSION)
	cat scilab >> ~/.Scilab/scilab-$(SCILAB_VERSION)/.scilab
