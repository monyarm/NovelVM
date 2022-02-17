MODULE := engines/smt

MODULE_OBJS := \
	smt.o \
	detection.o \
	metaengine.o \
	formats/archive/pac.o \
	formats/graphic/tmx.o \
	formats/script/bmd.o 

MODULE_DIRS += \
	engines/smt

# This module can be built as a plugin
ifeq ($(ENABLE_SMT), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk
