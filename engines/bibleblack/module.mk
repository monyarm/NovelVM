MODULE := engines/bibleblack

MODULE_OBJS := \
	bibleblack.o \
	detection.o \
	formats/archive/pak.o

MODULE_DIRS += \
	engines/bibleblack

# This module can be built as a plugin
ifeq ($(ENABLE_BIBLEBLACK), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk
