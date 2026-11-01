# Aether: AArch64 kernel and userspace. Normal boots never execute self-tests.
.DEFAULT_GOAL := all
include kernel/build.mk
.PHONY: all kernel userspace run test check clean check-fs-syscalls test-critical test-memory test-smp
all kernel: arm64
userspace: $(BUILD_DIR)/init.elf
run: run-arm64
test check: \
    check-memory-primitives \
    check-arm64-normal \
    check-arm64-user \
    check-arm64-memory \
    check-arm64-exceptions \
    check-arm64-mmu \
    check-arm64-runtime \
    check-arm64-scheduler \
    check-arm64-handoff \
    check-arm64-smp-panic
clean: clean-arm64
check-fs-syscalls: check-arm64-user
test-critical: check-arm64
test-memory: check-arm64-memory check-arm64-mmu
test-smp: check-arm64-smp

# Test recipes own bounded VMs and a shared test build directory. Serialize
# recursive checks even when the caller enables parallel compilation globally.
ifneq ($(filter test check check-% test-%,$(MAKECMDGOALS)),)
.NOTPARALLEL:
endif

.PHONY: format check-format
format:
	python3 util/format-code.py
check-format:
	python3 util/format-code.py --check

.PHONY: check-memory-primitives
check-memory-primitives:
	python3 util/test-memory-primitives.py
