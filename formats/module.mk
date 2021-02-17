MODULE := formats

MODULE_OBJS := \
	archive/cpk.o \
	archive/cvm.o \
	video/pmsf.o \
	graphic/dds.o \
	audio/adx.o

# Include common rules
include $(srcdir)/rules.mk
