all: install

SCILAB_VERSION = 4.1.1
SCILAB_DIR = /usr/local/scilab-$(SCILAB_VERSION)
MACROS_DIR = $(PWD)

install:
	cp -dpR RTAI    $(SCILAB_DIR)/macros
	(cd $(SCILAB_DIR)/macros/RTAI ; make)

user:
	mkdir -p ~/.Scilab/scilab-$(SCILAB_VERSION)
	cat scilab >> ~/.Scilab/scilab-$(SCILAB_VERSION)/.scilab
