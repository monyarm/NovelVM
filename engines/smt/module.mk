MODULE := engines/smt

MODULE_OBJS := \
	smt.o \
	metaengine.o \
	formats/archive/pac.o \
	formats/graphic/tmx.o \
	gfx/gfx.o \
	gfx/gfx_tinygl.o \
	gfx/gfx_tinygl_texture.o \
	gfx/gfx_opengl.o \
	gfx/gfx_opengl_texture.o \
	gfx/gfx_opengl_shaders.o \

MODULE_DIRS += \
	engines/smt

# This module can be built as a plugin
ifeq ($(ENABLE_SMT), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

DETECT_OBJS += $(MODULE)/detection.o