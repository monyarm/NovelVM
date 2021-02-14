MODULE := engines/koihime_doki

MODULE_OBJS := \
	koihime_doki.o \
	detection.o \
	formats/archive/exwlcs.o

MODULE_DIRS += \
	engines/koihime_doki

# This module can be built as a plugin
ifeq ($(ENABLE_KOIHIME_DOKI), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk
