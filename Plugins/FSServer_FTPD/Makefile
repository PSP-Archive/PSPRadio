BASE_DIR = ../..
include $(BASE_DIR)/include.mak

TARGET = FSS_FTPD

EXPORTED_OBJS = $(PSPRADIO_DIR)/PSPRadioExports.o 
OBJS = main.o ftp_main.o ftp.o _itoa.o sutils.o  \
		$(EXPORTED_OBJS)

# Define to build this as a prx (instead of a static elf)
BUILD_PRX=1
USE_PSPSDK_LIBC = 0
# Define the name of our custom exports (minus the .exp extension)
PRX_EXPORTS=$(PLUGINS_DIR)/FSS_Exports.exp

INCLUDES = -I./ -I$(PLUGINS_DIR) -I$(BASE_DIR) -I$(PSPRADIO_DIR) -I$(SHAREDLIB_DIR)/PSPApp -L$(SHAREDLIB_DIR)/lib -L$(PLUGINS_DIR)/Common\
		 -I$(SHAREDLIB_DIR)/libpspnet -I$(SHAREDLIB_DIR)/libiniparser \
		 -I$(SHAREDLIB_DIR)/Tools -I$(SHAREDLIB_DIR)/Logging -I$(PLUGINS_DIR)/Common
		 
CFLAGS = $(GLOBAL_CFLAGS) -O2 -g -Wall $(INCLUDES)
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBDIR =

LIBS =  -lstdc++  -lPSPApp -liniparser -lPluginCommon

all: $(TARGET).prx

install:
	mkdir -p $(PSPRADIO_RELEASE_DIR)/$(TARGET)
	cp -vf $(TARGET).prx $(PSPRADIO_RELEASE_DIR)
	cp -vf *.cfg $(PSPRADIO_RELEASE_DIR)/$(TARGET)

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
