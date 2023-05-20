ISO_NAME = jeff

.PHONY: all
all: $(ISO_NAME).iso

.PHONY: run run-uefi run-hdd run-hdd-uefi
run: $(ISO_NAME).iso
	qemu-system-x86_64 -M q35 -m 2G -cdrom $(ISO_NAME).iso -boot d -D qlog.txt -d int -no-reboot -monitor stdio -smp 4

limine:
	git clone https://github.com/limine-bootloader/limine.git --branch=v4.x-branch-binary --depth=1
	$(MAKE) -C limine

.PHONY: kernel
kernel:
	$(MAKE) -C kernel

base.img:
	dd if=/dev/zero of=base.img bs=1M count=10
	mkfs.ext4 base.img
	mkdir -p base_mnt
	sudo mount base.img base_mnt
	sudo cp -r base/* base_mnt/
	sudo umount base_mnt
	rm -rf base_mnt

$(ISO_NAME).iso: limine kernel base.img
	rm -rf iso_root
	mkdir -p iso_root
	cp kernel/kernel.elf \
		limine.cfg limine/limine.sys limine/limine-cd.bin limine/limine-cd-efi.bin base.img iso_root/
	xorriso -as mkisofs -b limine-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-cd-efi.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(ISO_NAME).iso
	limine/limine-deploy $(ISO_NAME).iso
	rm -rf iso_root

.PHONY: clean
clean:
	rm -rf iso_root $(ISO_NAME).iso base.img
	$(MAKE) -C kernel clean
	rm qlog.txt

.PHONY: distclean
distclean: clean
	rm -rf limine ovmf
	$(MAKE) -C kernel distclean
