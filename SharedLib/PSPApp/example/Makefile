TARGET = Example
OBJS = example.o

INCDIR = ../
CFLAGS = -O2 -G0 -Wall -L../../lib
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBDIR = 
LDFLAGS = 
LIBS = -lPSPApp -lpspaudiolib -lpspaudio -lm 

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PSPApp example App

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
