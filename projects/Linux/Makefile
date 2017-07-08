.PHONY: all NoDiceLib NoDice MusConv clean

all: NoDiceLib NoDice MusConv

###############################################################
# General
###############################################################

# NOTE: If compiling for a big-endian processor, remove "-DLSB_FIRST"

GCC := gcc
CFLAGS := -O3 -I ../../src/NoDiceLib -DLSB_FIRST
AR := ar
ARFLAGS := rcs


###############################################################
# NoDiceLib (Core library)
###############################################################

# NoDiceLib labels
NDLSRCPATH := ../../src/NoDiceLib/
NDLOBJPATH := obj/NoDiceLib/
NDLSOURCES := $(shell find $(NDLSRCPATH) -type f -name '*.c')
NDLOBJS := $(patsubst $(NDLSRCPATH)%, $(NDLOBJPATH)%, $(patsubst %.c,%.o,$(NDLSOURCES)))
NDLLIB := $(NDLOBJPATH)NoDiceLib.a

# NoDiceLib source and objects
$(NDLOBJPATH)%.o: $(NDLSRCPATH)%.c
	$(GCC) -c $(CFLAGS) $(NDLSRCPATH)$*.c -o $(NDLOBJPATH)$*.o

# NoDiceLib library
$(NDLLIB) : $(NDLOBJS)
	@echo Creating NoDiceLib library...
	$(AR) $(ARFLAGS) $(NDLLIB) $(NDLOBJS)

NoDiceLib: $(NDLLIB)


###############################################################
# NoDice (Level editor GUI)
###############################################################

# NoDice
GTKCFLAGS = $(shell pkg-config gtk+-2.0 --cflags)
GTKLIBS = $(shell pkg-config gtk+-2.0 --libs)
NDSRCPATH := ../../src/NoDice/
NDOBJPATH := obj/NoDice/
NDSOURCES := $(shell find $(NDSRCPATH) -type f -name '*.c')
NDOBJS := $(patsubst $(NDSRCPATH)%, $(NDOBJPATH)%, $(patsubst %.c,%.o,$(NDSOURCES)))
NDBIN := ../../bin/NoDice

# NoDice source and objects
$(NDOBJPATH)%.o: $(NDSRCPATH)%.c
	$(GCC) -c $(CFLAGS) $(GTKCFLAGS) $(NDSRCPATH)$*.c -o $(NDOBJPATH)$*.o

# NoDice binary
$(NDBIN) : $(NDOBJS) $(NDLLIB)
	@echo Creating NoDice executable...
	$(GCC) $(NDOBJS) -o $(NDBIN) $(NDLLIB) $(GTKLIBS)

NoDice: $(NDBIN)


###############################################################
# MusConv (MIDI -> SMB3)
###############################################################

# MusConv
MCSRCPATH := ../../src/MusConv/
MCOBJPATH := obj/MusConv/
MCSOURCES := $(shell find $(MCSRCPATH) -type f -name '*.c')
MCOBJS := $(patsubst $(MCSRCPATH)%, $(MCOBJPATH)%, $(patsubst %.c,%.o,$(MCSOURCES)))
MCBIN := ../../bin/MusConv

# MusConv source and objects
$(MCOBJPATH)%.o: $(MCSRCPATH)%.c
	$(GCC) -c $(CFLAGS) $(MCSRCPATH)$*.c -o $(MCOBJPATH)$*.o

# MusConv binary
$(MCBIN) : $(MCOBJS) $(MCLLIB)
	@echo Creating MusConv executable...
	$(GCC) $(MCOBJS) -o $(MCBIN) $(NDLLIB)

MusConv: $(MCBIN)


clean:
	rm -f `find $(NDLOBJPATH) -type f -name '*.o'`
	rm -f `find $(NDOBJPATH) -type f -name '*.o'`
	rm -f `find $(MCOBJPATH) -type f -name '*.o'`
	rm -f $(NDLLIB)
	rm -f $(NDBIN)
	rm -f $(MCBIN)
