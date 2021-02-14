MODULE := engines/smt

MODULE_OBJS := \
	smt.o \
	detection.o \
	formats/archive/cpk.o \
	formats/video/pmsf.o \
	formats/image/tmx.o \
	formats/archive/cvm/cvmfile.o \
	formats/archive/cvm/isodirectory.o

MODULE_DIRS += \
	engines/smt

# This module can be built as a plugin
ifeq ($(ENABLE_SMT), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk
