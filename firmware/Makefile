# customize the following paths for your computer
CC = xc8
LIB_INC_PATH = "./include"

CHIP = 16F1454

CFLAGS = --chip=$(CHIP) -Q -G  --double=24 --float=24
CFLAGS += --rom=default,-0-1FF,-1F7F-1F7F
CFLAGS += --codeoffset=0x200
CFLAGS += --opt=default,+asm,-asmfile,+speed,-space,-debug --addrqual=ignore
CFLAGS += --mode=pro -N64 -I. -I$(LIB_INC_PATH) --warn=0 --asmlist --summary=default,-psect,-class,+mem,-hex,-file --output=default,-inhx032 
CFLAGS += --runtime=default,+clear,+init,-keep,-no_startup,+osccal,-resetbits,-download,-stackcall,+clib

MIKSMS_OBJS = usb.p1 usb_cdc.p1 usb_descriptors.p1 main.p1 user.p1 usb_helpers.p1

MIKSMS_HDRS = usb_config.h

all: miksms.hex

miksms.hex: $(MIKSMS_OBJS)
	$(CC) $(CFLAGS) -o./$@ $(MIKSMS_OBJS)

%.p1: %.c $(MIKSMS_HDRS) Makefile
	$(CC) --pass1 $(CFLAGS) -o./$@ $<

clean:
#	rm -f miksms.hex
	rm -f *.p1 *.d *.pre *.sym *.cmf *.cof *.hxl *.lst *.obj *.rlf *.sdb
	rm -f funclist
