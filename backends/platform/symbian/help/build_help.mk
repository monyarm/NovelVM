# ============================================================================
#  Name	 : help.mk
#  Part of  : NovelVM
#
#  Description: This is file for creating .hlp file
# 
# ============================================================================


makmake :
	cshlpcmp NovelVM.xml

ifeq (WINS,$(findstring WINS, $(PLATFORM)))
	copy NovelVM.hlp $(EPOCROOT)epoc32\$(PLATFORM)\c\resource\help
endif

clean :
	del NovelVM.hlp
	del NovelVM.hlp.hrh

ifeq (WINS,$(findstring WINS, $(PLATFORM)))
	copy NovelVM.hlp $(EPOCROOT)epoc32\$(PLATFORM)\c\resource\help
endif

bld freeze lib cleanlib final resource savespace releasables :

