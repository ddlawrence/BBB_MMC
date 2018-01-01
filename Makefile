CROSSCOMPILE = arm-linux-gnueabihf-
# use arm-linux-gnueabihf  toolchain above for Linux based compiling:
# use arm-none-eabi  toolchain below for Windows based compiling:
# CROSSCOMPILE = arm-none-eabi-
.SUFFIXES:
.SUFFIXES: .c .s .h .o

OBJ=obj/

CFLAGS = -mcpu=cortex-a8 -marm -Wall -O2 -nostdlib -nostartfiles -ffreestanding \
  -fstack-usage -Wstack-usage=16384 -w -I drivers/ -I drivers_C/ -I drivers_C/include \
  -I mmc_lib/include -I mmc_lib/fatfs/src

all : rts.elf

$(OBJ)%.o : drivers/%.s
	$(CROSSCOMPILE)gcc $(CFLAGS) -c $< -o $@

$(OBJ)%.o : drivers_C/%.c
	$(CROSSCOMPILE)gcc $(CFLAGS) -c $< -o $@

$(OBJ)mmc_hwif.o : mmc_lib/mmc_hwif.c
	$(CROSSCOMPILE)gcc $(CFLAGS) -c $< -o $@

$(OBJ)mmc_api.o : mmc_lib/mmc_api.c
	$(CROSSCOMPILE)gcc $(CFLAGS) -c $< -o $@

$(OBJ)mmc_uif.o : mmc_lib/mmc_uif.c
	$(CROSSCOMPILE)gcc $(CFLAGS) -c $< -o $@

$(OBJ)diskio.o : mmc_lib/fatfs/port/diskio.c
	$(CROSSCOMPILE)gcc $(CFLAGS) -c $< -o $@

$(OBJ)ff.o : mmc_lib/fatfs/src/ff.c
	$(CROSSCOMPILE)gcc $(CFLAGS) -c $< -o $@

$(OBJ)main.o : main.c
	$(CROSSCOMPILE)gcc $(CFLAGS) -c $< -o $@

rts.elf : memmap.lds $(OBJ)*.o
	$(CROSSCOMPILE)ld -o rts.elf -T memmap.lds $(OBJ)startup.o $(OBJ)irq.o \
  $(OBJ)gpio.o $(OBJ)uart.o $(OBJ)mmc.o $(OBJ)tim.o $(OBJ)cp15.o $(OBJ)mclk.o \
  $(OBJ)libc.o $(OBJ)mmu.o $(OBJ)dma.o $(OBJ)mmc_hwif.o $(OBJ)mmc_api.o \
  $(OBJ)diskio.o $(OBJ)ff.o $(OBJ)mmc_uif.o $(OBJ)main.o

#	$(CROSSCOMPILE)objcopy rts.elf rts.bin -O srec
# srec format above for jtag loading
# binary format below for UART and SD card booting
	$(CROSSCOMPILE)objcopy rts.elf app_mmc -O binary
# for UART booting load file "app_mmc"
# for SD card booting prepend size and load_addr to binary with tiimage:
# tiimage 0x80000000 NONE app_mmc app
# copy file "app" to SD card (FAT32, bootable, 255 heads, 63 sectors)

	$(CROSSCOMPILE)objdump -M reg-names-raw -D rts.elf > rts.lst
	$(CROSSCOMPILE)objdump -d -S -h -t rts.elf > rts.dmp

clean :
	-@rm *.dmp *.lst *.elf
# NB never delete any object files from obj/ 
#    it will mess up the rule for making rts.elf
