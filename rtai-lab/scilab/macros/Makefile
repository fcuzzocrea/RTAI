all: install

SCILAB_DIR = /usr/lib/scilab-2.7
MACROS_DIR = $(PWD)

install:
	cp RTAICodeGen_.sci $(SCILAB_DIR)/macros/scicos
	patch $(SCILAB_DIR)/macros/scicos/Makefile $(MACROS_DIR)/Makefile.diff
	patch $(SCILAB_DIR)/macros/scicos/Linear.cosf $(MACROS_DIR)/Linear.cosf.diff
	patch $(SCILAB_DIR)/macros/scicos/Non_linear.cosf $(MACROS_DIR)/Non_linear.cosf.diff
	patch $(SCILAB_DIR)/macros/scicos/Branching.cosf $(MACROS_DIR)/Branching.cosf.diff
	patch $(SCILAB_DIR)/macros/scicos/Others.cosf $(MACROS_DIR)/Others.cosf.diff
	patch $(SCILAB_DIR)/macros/scicos/Sources.cosf $(MACROS_DIR)/Sources.cosf.diff
	patch $(SCILAB_DIR)/macros/util/initial_scicos_tables.sci $(MACROS_DIR)/initial_scicos_tables.sci.diff
	(cd $(SCILAB_DIR)/macros/scicos ; make)
	(cd $(SCILAB_DIR)/macros/util ; make)
