MODULE := engines/ai5

MODULE_OBJS := \
	ai5.o \
	anim.o \
	animplay.o \
	archive.o \
	asset.o \
	audio.o \
	backlog.o \
	cg.o \
	config.o \
	cursor.o \
	dungeon.o \
	effect.o \
	gcc.o \
	gfx.o \
	input.o \
	map.o \
	menu.o \
	mes.o \
	metaengine.o \
	movie.o \
	popupmenu.o \
	savedata.o \
	sys.o \
	text.o \
	vm.o \
	games/aishimai.o \
	games/classics.o \
	games/doukyuusei.o \
	games/isaku.o \
	games/isaku_item_window.o \
	games/kakyuusei.o \
	games/shangrlia.o \
	games/yuno.o

# This module can be built as a plugin
ifeq ($(ENABLE_AI5), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
