all: install

SCILAB_DIR = /usr/local/scilab-3.0
MACROS_DIR = $(PWD)

install:
	cp -dpR RTAI    $(SCILAB_DIR)/macros
	(cd $(SCILAB_DIR)/macros/RTAI ; make)

user:
	cat scilab >> ~/.scilab

