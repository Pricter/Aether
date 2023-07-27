all: KERNEL
KERNEL: kernel.elf

CC = gcc
LD = ld

KERNEL_CFLAGS  = -g -pipe -nostdinc -Wall -Wextra -std=gnu11 -O2 -Werror
KERNEL_CFLAGS += -DPRINTF_ALIAS_STANDARD_FUNCTION_NAMES_HARD -DSERIAL_LOG -DFLANTERM_FB_DISABLE_CANVAS -DDFLANTERM_FB_DISABLE_BUMP_ALLOC -DFLANTERM_FB_DISABLE_BUMP_ALLOC
KERNEL_CFLAGS += -D_KERNEL_ -DKERNEL_ARCH=x86_64 -DKERNEL_GIT_TAG=$(shell ../util/git-tag.sh)
KERNEL_CFLAGS += -ffreestanding -fno-stack-protector -fno-stack-check -fno-PIE
KERNEL_CFLAGS += -fno-lto -fno-PIC -m64 -march=x86-64 -mabi=sysv -mno-mmx
KERNEL_CFLAGS += -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel
KERNEL_CFLAGS += -I../base/usr/include -I../kernel -fno-omit-frame-pointer
KERNEL_CFLAGS += -Wno-unused-variable -Wno-unused-function -fsanitize=undefined -Wno-unused-parameter

KERNEL_LDFLAGS  = -nostdlib -static -m elf_x86_64 -no-pie
KERNEL_LDFLAGS += -z max-page-size=0x1000 -T linker.ld

KERNEL_CFILES := $(shell find -L . -type f -name '*.c')
KERNEL_ASMFILES := $(shell find -L . -type f -name '*.S')

KERNEL_OBJECTS := $(patsubst %.c,%.o,$(KERNEL_CFILES))
KERNEL_ASMOBJECTS := $(patsubst %.S,%.asm.o,$(KERNEL_ASMFILES))
KERNEL_ASMOBJECTS := $(filter-out ./symbols.o, $(KERNEL_ASMOBJECTS))

%.o: %.c
	@$(CC) $(KERNEL_CFLAGS) -c $< -o $@ > /dev/null
	@echo     CC -c $< -o $@

%.asm.o: %.S
	@$(CC) $(KERNEL_CFLAGS) -c $< -o $@
	@echo     CC -c $< -o $@

kernel.elf: $(KERNEL_OBJECTS) $(KERNEL_ASMOBJECTS)
	@echo Building kernel...
	$(LD) $(KERNEL_LDFLAGS) -o kernel.elf $(KERNEL_OBJECTS) $(KERNEL_ASMOBJECTS)
	@echo Kernel fully built!

.PHONY: clean
clean:
	-rm -f $(KERNEL_ASMOBJECTS) $(KERNEL_OBJECTS) symbols.o
	-rm -f kernel.elf