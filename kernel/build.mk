# The kernel targets AArch64 exclusively. Native fixtures use host Clang separately.
ifneq ($(filter default undefined,$(origin CC)),)
CC := clang
endif
ifneq ($(filter default undefined,$(origin LD)),)
LD := $(shell command -v ld.lld 2>/dev/null || { command -v brew >/dev/null 2>&1 && brew --prefix lld@22 2>/dev/null | sed 's|$$|/bin/ld.lld|'; })
endif
QEMU ?= qemu-system-aarch64
CPUS ?= 4
QEMU_DISPLAY ?= cocoa,zoom-to-fit=on,full-screen=on
CONSOLE_COLUMNS ?= 132
CONSOLE_ROWS ?= 40
KERNEL_SELF_TESTS ?= 0
ifeq ($(KERNEL_SELF_TESTS),1)
BUILD_DIR ?= build/tests
INIT_SOURCE := userspace/tests/init.c
else
BUILD_DIR ?= build/kernel
INIT_SOURCE := userspace/init/init.c
endif
KERNEL_CFLAGS := --target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only
# With translation disabled, unaligned accesses can fault even for RAM.
KERNEL_CFLAGS += -mstrict-align
KERNEL_CFLAGS += -ffreestanding -fno-builtin -fno-stack-protector -fno-pic
KERNEL_CFLAGS += -fno-omit-frame-pointer -MMD -MP
KERNEL_CFLAGS += -I$(BUILD_DIR)/generated
KERNEL_CFLAGS += -nostdinc -Ikernel/include -Ibase/usr/include -Wall -Wextra -Werror -O2 -g
ifeq ($(KERNEL_SELF_TESTS),1)
KERNEL_CFLAGS += -DKERNEL_SELF_TESTS
endif
KERNEL_OBJECTS := $(BUILD_DIR)/boot.o $(BUILD_DIR)/early.o $(BUILD_DIR)/serial.o
KERNEL_OBJECTS += $(BUILD_DIR)/vectors.o $(BUILD_DIR)/exception.o $(BUILD_DIR)/panic.o
KERNEL_OBJECTS += $(BUILD_DIR)/boot-memory.o $(BUILD_DIR)/memory.o
KERNEL_OBJECTS += $(BUILD_DIR)/frames.o $(BUILD_DIR)/mmu.o
KERNEL_OBJECTS += $(BUILD_DIR)/page-backend.o $(BUILD_DIR)/slab.o
KERNEL_OBJECTS += $(BUILD_DIR)/spinlock.o $(BUILD_DIR)/interrupts.o $(BUILD_DIR)/time.o
KERNEL_OBJECTS += $(BUILD_DIR)/runtime.o
KERNEL_OBJECTS += $(BUILD_DIR)/cpu.o $(BUILD_DIR)/switch.o
KERNEL_OBJECTS += $(BUILD_DIR)/scheduler.o $(BUILD_DIR)/kstack.o
ifeq ($(KERNEL_SELF_TESTS),1)
KERNEL_OBJECTS += $(BUILD_DIR)/scheduler-tests.o $(BUILD_DIR)/switch-tests.o
KERNEL_OBJECTS += $(BUILD_DIR)/smp-tests.o $(BUILD_DIR)/user-tests.o $(BUILD_DIR)/userspace-boot.o
else
KERNEL_OBJECTS += $(BUILD_DIR)/startup.o
endif
KERNEL_OBJECTS += $(BUILD_DIR)/smp.o $(BUILD_DIR)/secondary.o
KERNEL_OBJECTS += $(BUILD_DIR)/user.o $(BUILD_DIR)/user-execution.o $(BUILD_DIR)/user-entry.o
KERNEL_OBJECTS += $(BUILD_DIR)/fs-vfs.o $(BUILD_DIR)/fs-tmpfs.o
KERNEL_OBJECTS += $(BUILD_DIR)/fs-devfs.o $(BUILD_DIR)/fs-pipe.o $(BUILD_DIR)/strings.o
KERNEL_OBJECTS += $(BUILD_DIR)/syscalls.o $(BUILD_DIR)/elf.o $(BUILD_DIR)/process.o
KERNEL_OBJECTS += $(BUILD_DIR)/filesystems.o $(BUILD_DIR)/init-embed.o
QEMU_ARGS := -machine virt,secure=off,virtualization=off,gic-version=2
QEMU_ARGS += -accel tcg -cpu cortex-a72 -smp $(CPUS) -m 256M
# The interactive window shows the PL011 text console; automated checks keep
# their independent headless/stdio arguments in check-arm64-boot.py.
QEMU_ARGS += -display $(QEMU_DISPLAY) -monitor none
QEMU_ARGS += -serial vc:$(CONSOLE_COLUMNS)Cx$(CONSOLE_ROWS)C -no-reboot -name Aether

.PHONY: arm64 run-arm64 check-arm64 clean-arm64
arm64: $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/Image

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/boot.o: kernel/boot/entry.S kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/early.o: kernel/kernel.c kernel/include/kernel/boot_memory.h kernel/include/platform/serial.h kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/serial.o: kernel/platform/qemu-virt/serial.c kernel/include/platform/serial.h kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel.elf: $(KERNEL_OBJECTS) kernel/linker.ld
	$(LD) -nostdlib -static -z max-page-size=4096 -T kernel/linker.ld $(KERNEL_OBJECTS) -o $@

run-arm64: arm64
	$(QEMU) $(QEMU_ARGS) $(QEMU_DEBUG) -kernel $(BUILD_DIR)/Image

ifeq ($(KERNEL_SELF_TESTS),1)
check-arm64: arm64
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/Image
endif

clean-arm64:
	rm -rf build/kernel build/tests

$(BUILD_DIR)/vectors.o: kernel/sys/vectors.S kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/exception.o: kernel/debug/exception.c kernel/include/kernel/exception.h kernel/include/platform/serial.h kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

# Fault injection has separate objects and images; normal boot never faults.
$(BUILD_DIR)/early-fault.o: kernel/kernel.c kernel/include/kernel/boot_memory.h kernel/include/platform/serial.h kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DARM64_TEST_EXCEPTION -c $< -o $@

$(BUILD_DIR)/exception-nested.o: kernel/debug/exception.c kernel/include/kernel/exception.h kernel/include/platform/serial.h kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DARM64_TEST_NESTED_EXCEPTION -c $< -o $@

$(BUILD_DIR)/fault.elf: $(filter-out $(BUILD_DIR)/early.o,$(KERNEL_OBJECTS)) $(BUILD_DIR)/early-fault.o kernel/linker.ld
	$(LD) -nostdlib -static -z max-page-size=4096 -T kernel/linker.ld $(filter %.o,$^) -o $@

$(BUILD_DIR)/nested-fault.elf: $(filter-out $(BUILD_DIR)/early.o $(BUILD_DIR)/exception.o,$(KERNEL_OBJECTS)) $(BUILD_DIR)/early-fault.o $(BUILD_DIR)/exception-nested.o kernel/linker.ld
	$(LD) -nostdlib -static -z max-page-size=4096 -T kernel/linker.ld $(filter %.o,$^) -o $@

.PHONY: check-arm64-exceptions
ifeq ($(KERNEL_SELF_TESTS),1)
check-arm64-exceptions: $(BUILD_DIR)/fault.Image $(BUILD_DIR)/nested-fault.Image
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/fault.Image exception
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/nested-fault.Image nested
endif

.PHONY: check-arm64-memory
ifeq ($(KERNEL_SELF_TESTS),1)
check-arm64-memory: arm64
	python3 util/test-boot-memory.py
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/Image boot 128
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/Image boot 512
endif

$(BUILD_DIR)/boot-memory.o: kernel/boot/memory.c kernel/include/kernel/boot_memory.h kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/memory.o: kernel/libc/memory.c kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Image: $(KERNEL_OBJECTS) kernel/linker.ld
	$(LD) --oformat=binary -nostdlib -static -z max-page-size=4096 -T kernel/linker.ld $(filter %.o,$^) -o $@

$(BUILD_DIR)/fault.Image: $(filter-out $(BUILD_DIR)/early.o,$(KERNEL_OBJECTS)) $(BUILD_DIR)/early-fault.o kernel/linker.ld
	$(LD) --oformat=binary -nostdlib -static -z max-page-size=4096 -T kernel/linker.ld $(filter %.o,$^) -o $@

$(BUILD_DIR)/nested-fault.Image: $(filter-out $(BUILD_DIR)/early.o $(BUILD_DIR)/exception.o,$(KERNEL_OBJECTS)) $(BUILD_DIR)/early-fault.o $(BUILD_DIR)/exception-nested.o kernel/linker.ld
	$(LD) --oformat=binary -nostdlib -static -z max-page-size=4096 -T kernel/linker.ld $(filter %.o,$^) -o $@

$(BUILD_DIR)/frames.o: kernel/boot/frames.c kernel/include/kernel/frame_allocator.h kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/mmu.o: kernel/memory/mmu.c kernel/include/kernel/mmu.h kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

ARM64_MMU_TESTS := unmapped readonly execute rollback
$(BUILD_DIR)/early-mmu-unmapped.o: ARM64_MMU_TEST_FLAGS = -DARM64_TEST_MMU_UNMAPPED
$(BUILD_DIR)/early-mmu-readonly.o: ARM64_MMU_TEST_FLAGS = -DARM64_TEST_MMU_READONLY
$(BUILD_DIR)/early-mmu-execute.o: ARM64_MMU_TEST_FLAGS = -DARM64_TEST_MMU_EXECUTE
$(BUILD_DIR)/early-mmu-rollback.o: ARM64_MMU_TEST_FLAGS = -DARM64_TEST_MMU_ROLLBACK

$(BUILD_DIR)/early-mmu-%.o: kernel/kernel.c kernel/build.mk $(BUILD_DIR)/generated/kernel/build_version.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) $(ARM64_MMU_TEST_FLAGS) -c $< -o $@

$(BUILD_DIR)/mmu-%.Image: $(filter-out $(BUILD_DIR)/early.o,$(KERNEL_OBJECTS)) $(BUILD_DIR)/early-mmu-%.o kernel/linker.ld
	$(LD) --oformat=binary -nostdlib -static -z max-page-size=4096 -T kernel/linker.ld $(filter %.o,$^) -o $@

.PHONY: check-arm64-mmu
ifeq ($(KERNEL_SELF_TESTS),1)
check-arm64-mmu: $(ARM64_MMU_TESTS:%=$(BUILD_DIR)/mmu-%.Image)
	python3 util/test-frame-allocator.py
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/mmu-unmapped.Image mmu-unmapped
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/mmu-readonly.Image mmu-readonly
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/mmu-execute.Image mmu-execute
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/mmu-rollback.Image mmu-rollback
endif

$(BUILD_DIR)/page-backend.o: kernel/memory/pages.c kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/slab.o: kernel/memory/slab.c kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/spinlock.o: kernel/lib/spinlock.c kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/interrupts.o: kernel/sys/interrupts.c kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/time.o: kernel/time/timer.c kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/runtime.o: kernel/sys/runtime.c kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/cpu.o: kernel/sys/cpu.c kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/switch.o: kernel/sys/switch.S kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/scheduler.o: kernel/sys/scheduler.c kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/kstack.o: kernel/memory/kstack.c kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/scheduler-tests.o: kernel/test/scheduler.c kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/switch-tests.o: kernel/test/switch.S kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

-include $(wildcard $(BUILD_DIR)/*.d)

# Included dependency files are generated only alongside compilation, never by
# make's built-in implicit executable rules during included-makefile remaking.
$(BUILD_DIR)/%.d: ;

$(BUILD_DIR)/smp.o: kernel/sys/smp.c kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/secondary.o: kernel/sys/secondary.S kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/smp-tests.o: kernel/test/smp.c kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/runtime-heap-irq.o: ARM64_RUNTIME_TEST_FLAGS = -DARM64_TEST_HEAP_IRQ
$(BUILD_DIR)/runtime-heap-double-free.o: ARM64_RUNTIME_TEST_FLAGS = -DARM64_TEST_HEAP_DOUBLE_FREE

$(BUILD_DIR)/runtime-%.o: kernel/sys/runtime.c kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) $(ARM64_RUNTIME_TEST_FLAGS) -c $< -o $@

$(BUILD_DIR)/runtime-%.Image: $(filter-out $(BUILD_DIR)/runtime.o,$(KERNEL_OBJECTS)) $(BUILD_DIR)/runtime-%.o kernel/linker.ld
	$(LD) --oformat=binary -nostdlib -static -z max-page-size=4096 -T kernel/linker.ld $(filter %.o,$^) -o $@

.PHONY: check-arm64-runtime
ifeq ($(KERNEL_SELF_TESTS),1)
check-arm64-runtime: arm64 $(BUILD_DIR)/runtime-heap-irq.Image $(BUILD_DIR)/runtime-heap-double-free.Image
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/Image
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/runtime-heap-irq.Image heap-irq
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/runtime-heap-double-free.Image heap-double-free
endif

$(BUILD_DIR)/scheduler-tests-lock.o: ARM64_SCHEDULER_TEST_FLAGS = -DARM64_TEST_SCHEDULER_LOCK
$(BUILD_DIR)/scheduler-tests-irq.o: ARM64_SCHEDULER_TEST_FLAGS = -DARM64_TEST_SCHEDULER_IRQ

$(BUILD_DIR)/scheduler-tests-%.o: kernel/test/scheduler.c kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) $(ARM64_SCHEDULER_TEST_FLAGS) -c $< -o $@

$(BUILD_DIR)/scheduler-%.Image: $(filter-out $(BUILD_DIR)/scheduler-tests.o,$(KERNEL_OBJECTS)) $(BUILD_DIR)/scheduler-tests-%.o kernel/linker.ld
	$(LD) --oformat=binary -nostdlib -static -z max-page-size=4096 -T kernel/linker.ld $(filter %.o,$^) -o $@

.PHONY: check-arm64-scheduler
ifeq ($(KERNEL_SELF_TESTS),1)
check-arm64-scheduler: arm64 $(BUILD_DIR)/scheduler-lock.Image $(BUILD_DIR)/scheduler-irq.Image
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/Image
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/scheduler-lock.Image scheduler-lock
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/scheduler-irq.Image scheduler-irq
endif

.PHONY: check-arm64-smp
ifeq ($(KERNEL_SELF_TESTS),1)
check-arm64-smp: arm64
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/Image boot 128 2
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/Image boot 256 4
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/Image boot 512 8
endif

$(BUILD_DIR)/scheduler-handoff.o: kernel/sys/scheduler.c kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DARM64_TEST_HANDOFF -c $< -o $@

$(BUILD_DIR)/smp-tests-handoff.o: kernel/test/smp.c kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DARM64_TEST_HANDOFF -c $< -o $@

$(BUILD_DIR)/handoff.Image: $(filter-out $(BUILD_DIR)/scheduler.o $(BUILD_DIR)/smp-tests.o,$(KERNEL_OBJECTS)) $(BUILD_DIR)/scheduler-handoff.o $(BUILD_DIR)/smp-tests-handoff.o kernel/linker.ld
	$(LD) --oformat=binary -nostdlib -static -z max-page-size=4096 -T kernel/linker.ld $(filter %.o,$^) -o $@

.PHONY: check-arm64-handoff
ifeq ($(KERNEL_SELF_TESTS),1)
check-arm64-handoff: $(BUILD_DIR)/handoff.Image
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/handoff.Image handoff 256 2
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/handoff.Image handoff 256 4
endif

$(BUILD_DIR)/smp-tests-panic.o: kernel/test/smp.c kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DARM64_TEST_SMP_PANIC -c $< -o $@

$(BUILD_DIR)/page-backend-panic.o: kernel/debug/panic.c kernel/build.mk | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DARM64_TEST_PANIC_PEERS -c $< -o $@

$(BUILD_DIR)/smp-panic.Image: $(filter-out $(BUILD_DIR)/smp-tests.o $(BUILD_DIR)/panic.o,$(KERNEL_OBJECTS)) $(BUILD_DIR)/smp-tests-panic.o $(BUILD_DIR)/page-backend-panic.o kernel/linker.ld
	$(LD) --oformat=binary -nostdlib -static -z max-page-size=4096 -T kernel/linker.ld $(filter %.o,$^) -o $@

.PHONY: check-arm64-smp-panic
ifeq ($(KERNEL_SELF_TESTS),1)
check-arm64-smp-panic: $(BUILD_DIR)/smp-panic.Image
	python3 util/check-arm64-boot.py $(QEMU) $(BUILD_DIR)/smp-panic.Image smp-panic 256 4
endif

$(BUILD_DIR)/user.o: kernel/memory/user.c
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/user-entry.o: kernel/sys/user_entry.S
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/user-tests.o: kernel/test/user.c
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

.PHONY: check-arm64-user
# EL0 validation runs only in the separate self-test images.
ifeq ($(KERNEL_SELF_TESTS),1)
check-arm64-user: check-arm64 check-arm64-smp
endif

ZIG ?= zig
USERSPACE_TARGET := $(strip $(shell cat docs/userspace-target.txt))
$(BUILD_DIR)/init.elf: $(INIT_SOURCE) docs/userspace-target.txt | $(BUILD_DIR)
	mkdir -p build/zig-cache build/zig-local
	ZIG_GLOBAL_CACHE_DIR=$(CURDIR)/build/zig-cache ZIG_LOCAL_CACHE_DIR=$(CURDIR)/build/zig-local $(ZIG) cc -target $(USERSPACE_TARGET) -static -fno-pie -Wl,-no-pie -Wl,-z,max-page-size=4096 -O2 -g -Wall -Wextra -Werror $< -o $@
ifeq ($(KERNEL_SELF_TESTS),1)
BUSYBOX_DEPENDENCY :=
else
BUSYBOX_DEPENDENCY := build/vendor/busybox-1.36.1/busybox
endif
$(BUILD_DIR)/init-embed.o: kernel/init/embed.S $(BUILD_DIR)/init.elf $(BUSYBOX_DEPENDENCY)
	$(CC) $(KERNEL_CFLAGS) -DARM64_BUSYBOX_IMAGE_PATH='"build/vendor/busybox-1.36.1/busybox"' -DARM64_INIT_IMAGE_PATH='"$(BUILD_DIR)/init.elf"' -c $< -o $@
$(BUILD_DIR)/fs-%.o: kernel/fs/%.c
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/strings.o: kernel/libc/strings.c
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/syscalls.o: kernel/sys/syscalls.c
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/elf.o: kernel/sys/elf_loader.c
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/process.o: kernel/sys/process.c
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/userspace-boot.o: kernel/test/userspace.c
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

ifneq ($(KERNEL_SELF_TESTS),1)
check-arm64 check-arm64-exceptions check-arm64-memory check-arm64-mmu check-arm64-runtime check-arm64-scheduler check-arm64-smp check-arm64-handoff check-arm64-smp-panic check-arm64-user:
	$(MAKE) KERNEL_SELF_TESTS=1 $@
endif

$(BUILD_DIR)/startup.o: kernel/sys/startup.c
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/filesystems.o: kernel/fs/init.c
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

.PHONY: check-arm64-normal
# This check deliberately targets normal startup, never the self-test image.
check-arm64-normal:
	$(MAKE) KERNEL_SELF_TESTS=0 arm64
	python3 util/check-arm64-normal-boot.py $(QEMU) build/kernel/Image

$(BUILD_DIR)/panic.o: kernel/debug/panic.c
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user-execution.o: kernel/sys/user.c
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

.PHONY: busybox
busybox: build/vendor/busybox-1.36.1/busybox
build/vendor/busybox-1.36.1/busybox: util/build-busybox.py util/busybox-cc.py docs/userspace-target.txt kernel/include/kernel/version.h
	python3 util/build-busybox.py

# One metadata snapshot is shared by early boot and uname within this build.
.PHONY: force-kernel-version
force-kernel-version:
$(BUILD_DIR)/generated/kernel/build_version.h: force-kernel-version util/kernel-version.py
	python3 util/kernel-version.py $@
$(BUILD_DIR)/early.o $(BUILD_DIR)/early-fault.o $(BUILD_DIR)/syscalls.o: $(BUILD_DIR)/generated/kernel/build_version.h
