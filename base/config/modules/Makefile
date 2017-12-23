# Copyright (C) 2005-2017 The RTAI project
# This [file] is free software; the RTAI project
# gives unlimited permission to copy and/or distribute it,
# with or without modifications, as long as this notice is preserved.
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY, to the extent permitted by law; without
# even the implied warranty of MERCHANTABILITY or FITNESS FOR A
# PARTICULAR PURPOSE.

all:
	$(MAKE) -s -C $(RTAI_LINUX_DIR) CC=$(CC) $(RTAI_LINUX_DIR)/include/linux/modversions.h
	$(MAKE) -s -C $(RTAI_LINUX_DIR) CC=$(CC) ARCH=$(ARCH) SUBDIRS=$(shell if [ "$$PWD" != "" ]; then echo $$PWD; else pwd; fi) modules

modules:
	@echo RTAI_KMOD_CFLAGS="\"$(CFLAGS) -DEXPORT_SYMTAB -Wall\""
