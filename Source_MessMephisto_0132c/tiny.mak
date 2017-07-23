###########################################################################
#
#   tiny.mak
#
#   Small driver-specific example makefile
#	Use make TARGET=mess SUBTARGET=tiny to build
#
#   As an example this makefile builds MESS with the three Colecovision
#   drivers enabled only.
#
#   Copyright (c) 1996-2007, Nicola Salmoria and the MAME Team.
#   Visit  http://mamedev.org for licensing and usage restrictions.
#
###########################################################################


# include MESS core defines
include $(SRC)/mess/messcore.mak
include $(SRC)/mess/osd/$(OSD)/$(OSD).mak


#-------------------------------------------------
# Specify all the CPU cores necessary for the
# drivers referenced in tiny.c.
#-------------------------------------------------

CPUS += M6502
CPUS += M680X0
CPUS += MCS48
CPUS += Z80



#-------------------------------------------------
# Specify all the sound cores necessary for the
# drivers referenced in tiny.c.
#-------------------------------------------------

SOUNDS += BEEP



#-------------------------------------------------
# This is the list of files that are necessary
# for building all of the drivers referenced
# in tiny.c
#-------------------------------------------------

DRVLIBS = \
	$(MESSOBJ)/tiny.o \
	$(MESSOBJ)/glasgow.a \
	$(MESSOBJ)/mephisto.a \
#	$(MESS_MACHINE)/glasgow.o \
	$(MESS_DRIVERS)/glasgow.o \
	$(MESS_DRIVERS)/mephisto.o \
#	$(EMU_VIDEO)/glasgow.o \
#	$(MESS_DEVICES)/glasgow.o \

$(MESSOBJ)/glasgow.a:      \
	$(MESS_DRIVERS)/glasgow.o

$(MESSOBJ)/mephisto.a:      \
	$(MESS_DRIVERS)/mephisto.o



#-------------------------------------------------
# layout dependencies
#-------------------------------------------------


$(MESS_DRIVERS)/glasgow.o:	$(MESS_LAYOUT)/glasgow.lh
$(MESS_DRIVERS)/mephisto.o:	$(MESS_LAYOUT)/mephisto.lh

#-------------------------------------------------
# MESS special OSD rules
#-------------------------------------------------

include $(SRC)/mess/osd/$(OSD)/$(OSD).mak
