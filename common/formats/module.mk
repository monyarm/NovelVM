MODULE := common/formats

MODULE_OBJS := \
	cue.o \
	disk_image.o \
	formatinfo.o \
	iff_container.o \
	ini-file.o \
	json.o \
	markdown.o \
	po_parser.o \
	prodos.o \
	quicktime.o \
	spectrum_tape.o \
	winexe.o \
	winexe_ne.o \
	winexe_pe.o \
	xmlparser.o \
	acb.o \
	cpk.o \
	cvm.o \
	pmsf.o \
	dds.o \
	adx.o \
	acx.o \
	hca.o \
	ahx.o \
	utf_table.o \
	afs.o \
	awb.o \
	cri_mask.o

# Include common rules
include $(srcdir)/rules.mk
