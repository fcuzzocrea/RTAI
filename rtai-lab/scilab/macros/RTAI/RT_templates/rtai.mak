# Makefile generate from template rtai.mak
# ========================================

all: ../$$MODEL$$

RTAIDIR = $(shell rtai-config --prefix)
C_FLAGS = $(shell rtai-config --lxrt-cflags)
SCIDIR = $$SCILAB_DIR$$

RM = rm -f
FILES_TO_CLEAN = *.o ../$$MODEL$$

CC = gcc
CC_OPTIONS = -O -DNDEBUG -Dlinux -DNARROWPROTO -D_GNU_SOURCE

MODEL = $$MODEL$$
OBJSSTAN = $$OBJ$$

SCILIBS = $(SCIDIR)/libs/scicos.a $(SCIDIR)/libs/lapack.a $(SCIDIR)/libs/poly.a $(SCIDIR)/libs/calelm.a $(SCIDIR)/libs/blas.a $(SCIDIR)/libs/lapack.a
OTHERLIBS = 
ULIBRARY = $(RTAIDIR)/lib/libsciblk.a $(RTAIDIR)/lib/liblxrt.a

CFLAGS = $(CC_OPTIONS) -O2 -I$(SCIDIR)/routines $(C_FLAGS) -I$(RTAIDIR)/include/scicos -DMODEL=$(MODEL)

rtmain.c: $(RTAIDIR)/share/rtai/scicos/rtmain.c $(MODEL)_standalone.c $(MODEL)_io.c
	cp $< .

../$$MODEL$$: $(OBJSSTAN) $(ULIBRARY)
	gcc -static -o $@  $(OBJSSTAN) $(SCILIBS) $(ULIBRARY) -lpthread -lm
	@echo "### Created executable: $(MODEL) ###"

clean::
	@$(RM) $(FILES_TO_CLEAN)
