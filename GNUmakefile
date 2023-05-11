ISO_NAME = jeff

.PHONY: all all-hdd
all: $(ISO_NAME).iso

all-hdd: $(ISO_NAME).hdd

.PHONY: run run-uefi run-hdd run-hdd-uefi
run: $(ISO_NAME).iso
	qemu-system-x86_64 -M q35 -m 2G -cdrom $(ISO_NAME).iso -boot d -D qlog.txt -d int -no-reboot

run-uefi: ovmf $(ISO_NAME).iso
	qemu-system-x86_64 -M q35 -m 2G -bios ovmf/OVMF.fd -cdrom $(ISO_NAME).iso -boot d

run-hdd: $(ISO_NAME).hdd
	qemu-system-x86_64 -M q35 -m 2G -hda $(ISO_NAME).hdd

run-hdd-uefi: ovmf $(ISO_NAME).hdd
	qemu-system-x86_64 -M q35 -m 2G -bios ovmf/OVMF.fd -hda $(ISO_NAME).hdd

ovmf:
	mkdir -p ovmf
	cd ovmf && curl -Lo OVMF-X64.zip https://efi.akeo.ie/OVMF/OVMF-X64.zip && unzip OVMF-X64.zip

limine:
	git clone https://github.com/limine-bootloader/limine.git --branch=v4.x-branch-binary --depth=1
	$(MAKE) -C limine

.PHONY: kernel
kernel:
	$(MAKE) -C kernel

$(ISO_NAME).iso: limine kernel
	rm -rf iso_root
	mkdir -p iso_root
	cp kernel/kernel.elf \
		limine.cfg limine/limine.sys limine/limine-cd.bin limine/limine-cd-efi.bin iso_root/
	xorriso -as mkisofs -b limine-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-cd-efi.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(ISO_NAME).iso
	limine/limine-deploy $(ISO_NAME).iso
	rm -rf iso_root

$(ISO_NAME).hdd: limine kernel
	rm -f $(ISO_NAME).hdd
	dd if=/dev/zero bs=1M count=0 seek=64 of=$(ISO_NAME).hdd
	parted -s $(ISO_NAME).hdd mklabel gpt
	parted -s $(ISO_NAME).hdd mkpart ESP fat32 2048s 100%
	parted -s $(ISO_NAME).hdd set 1 esp on
	limine/limine-deploy $(ISO_NAME).hdd
	sudo losetup -Pf --show $(ISO_NAME).hdd >loopback_dev
	sudo mkfs.fat -F 32 `cat loopback_dev`p1
	mkdir -p img_mount
	sudo mount `cat loopback_dev`p1 img_mount
	sudo mkdir -p img_mount/EFI/BOOT
	sudo cp -v kernel/kernel.elf limine.cfg limine/limine.sys img_mount/
	sudo cp -v limine/BOOTX64.EFI img_mount/EFI/BOOT/
	sync
	sudo umount img_mount
	sudo losetup -d `cat loopback_dev`
	rm -rf loopback_dev img_mount

.PHONY: clean
clean:
	rm -rf iso_root $(ISO_NAME).iso $(ISO_NAME).hdd
	$(MAKE) -C kernel clean
	rm qlog.txt

.PHONY: distclean
distclean: clean
	rm -rf limine ovmf
	$(MAKE) -C kernel distclean
