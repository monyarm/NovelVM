MODULE := engines/ikura

MODULE_OBJS := \
	ikura.o \
	metaengine.o \
	formats/archive/cabinet.o \
	formats/graphic/ggp.o \
	formats/graphic/ggd24.o \
	formats/graphic/ggd8.o \
	formats/graphic/gga.o \
	formats/graphic/gan.o \
	formats/script/script.o \
	script/context.o \
	script/interpreter.o

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
