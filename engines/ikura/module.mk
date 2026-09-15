MODULE := engines/ikura

MODULE_OBJS := \
	ikura.o \
	metaengine.o \
	formats/archive/cabinet.o

MODULE_DIRS += \
	engines/ikura

# This module can be built as a plugin
ifeq ($(ENABLE_IKURA), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
