all: install

SCILAB_DIR = /usr/local/scilab-3.1
MACROS_DIR = $(PWD)

install:
	cp -dpR RTAI    $(SCILAB_DIR)/macros
	(cd $(SCILAB_DIR)/macros/RTAI ; make)

user:
	cat scilab >> ~/.Scilab/scilab-3.1/.scilab

