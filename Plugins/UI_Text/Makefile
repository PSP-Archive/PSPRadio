BASE_DIR = ../..
include $(BASE_DIR)/include.mak

TARGET = UI_Text

EXPORTED_OBJS = $(PSPRADIO_DIR)/PSPRadioExports.o 
OBJS = main.o TextUI.o $(PLUGINS_DIR)/UI_Interface.o $(EXPORTED_OBJS)

# Define to build this as a prx (instead of a static elf)
BUILD_PRX=1
# Define the name of our custom exports (minus the .exp extension)
PRX_EXPORTS=$(PLUGINS_DIR)/UI_Exports.exp

INCLUDES = -I./ -I$(PLUGINS_DIR) -I$(BASE_DIR) -I$(PSPRADIO_DIR) -I$(SHAREDLIB_DIR)/PSPApp -L$(SHAREDLIB_DIR)/lib \
		 -I$(SHAREDLIB_DIR)/libpspnet -I$(SHAREDLIB_DIR)/libiniparser \
		 -I$(SHAREDLIB_DIR)/libscreen -I$(SHAREDLIB_DIR)/Logging \
		 -I$(PSPRADIO_DIR)/ScreenHandler -L$(PLUGINS_DIR)/Common

		 
CFLAGS = $(GLOBAL_CFLAGS) -O2 -g -Wall $(INCLUDES) 
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBDIR =


LIBS = -lstdc++ -lpthreadlite -lguscreen -lpspvalloc -lpspgu -liniparser -lPluginCommon  -lpng -lz -lm

all: $(TARGET).prx

install: all
	mkdir -p $(PSPRADIO_RELEASE_DIR)/$(TARGET)
	cp -vf $(TARGET).prx $(PSPRADIO_RELEASE_DIR)
	cp -vf *.cfg $(PSPRADIO_RELEASE_DIR)/$(TARGET)
	cp -vf *.png $(PSPRADIO_RELEASE_DIR)/$(TARGET)

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
