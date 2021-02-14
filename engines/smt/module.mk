MODULE := engines/smt

MODULE_OBJS := \
	smt.o \
	gfx.o \
	gfx_opengl.o \
	gfx_opengl_texture.o \
	gfx_opengl_shaders.o \
	gfx_tinygl.o \
	gfx_tinygl_texture.o \
	detection.o \
	formats/archive/cpk.o \
	formats/archive/cvm.o \
	formats/archive/pac.o \
	formats/video/pmsf.o \
	formats/image/tmx.o \
	formats/image/dds.o \
	formats/audio/adx.o \

MODULE_DIRS += \
	engines/smt

# This module can be built as a plugin
ifeq ($(ENABLE_SMT), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk
