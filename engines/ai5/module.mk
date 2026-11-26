MODULE := engines/ai5

MODULE_OBJS := \
	ai5.o \
	anim.o \
	archive.o \
	asset.o \
	cg.o \
	config.o \
	gcc.o \
	metaengine.o

# This module can be built as a plugin
ifeq ($(ENABLE_AI5), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
