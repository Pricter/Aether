# Build the normal static AArch64 musl init through the canonical root build.
.DEFAULT_GOAL := all
.PHONY: all clean
all:
	$(MAKE) -C ../.. userspace
clean:
	rm -f ../../build/kernel/init.elf
