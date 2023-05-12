.section .symbols

.extern bitmap
.type bitmap, @function
.extern bytesOfBitmap
.type bytesOfBitmap, @function
.extern data_end
.type data_end, @function
.extern data_start
.type data_start, @function
.extern fatal
.type fatal, @function
.extern fctprintf
.type fctprintf, @function
.extern freeMemory
.type freeMemory, @function
.extern gdt
.type gdt, @function
.extern gdt_init
.type gdt_init, @function
.extern gdt_reload
.type gdt_reload, @function
.extern hhdm_request
.type hhdm_request, @function
.extern idt_init
.type idt_init, @function
.extern idt_set_gate
.type idt_set_gate, @function
.extern _isr0
.type _isr0, @function
.extern _isr1
.type _isr1, @function
.extern _isr10
.type _isr10, @function
.extern _isr11
.type _isr11, @function
.extern _isr12
.type _isr12, @function
.extern _isr128
.type _isr128, @function
.extern _isr13
.type _isr13, @function
.extern _isr14
.type _isr14, @function
.extern _isr15
.type _isr15, @function
.extern _isr16
.type _isr16, @function
.extern _isr17
.type _isr17, @function
.extern _isr18
.type _isr18, @function
.extern _isr19
.type _isr19, @function
.extern _isr2
.type _isr2, @function
.extern _isr20
.type _isr20, @function
.extern _isr21
.type _isr21, @function
.extern _isr22
.type _isr22, @function
.extern _isr23
.type _isr23, @function
.extern _isr24
.type _isr24, @function
.extern _isr25
.type _isr25, @function
.extern _isr26
.type _isr26, @function
.extern _isr27
.type _isr27, @function
.extern _isr28
.type _isr28, @function
.extern _isr29
.type _isr29, @function
.extern _isr3
.type _isr3, @function
.extern _isr30
.type _isr30, @function
.extern _isr31
.type _isr31, @function
.extern _isr4
.type _isr4, @function
.extern _isr5
.type _isr5, @function
.extern _isr6
.type _isr6, @function
.extern _isr7
.type _isr7, @function
.extern _isr8
.type _isr8, @function
.extern _isr9
.type _isr9, @function
.extern isr_common
.type isr_common, @function
.extern isr_handler
.type isr_handler, @function
.extern isr_handler_inner
.type isr_handler_inner, @function
.extern __kernel_arch
.type __kernel_arch, @function
.extern __kernel_build_date
.type __kernel_build_date, @function
.extern __kernel_build_time
.type __kernel_build_time, @function
.extern __kernel_compiler_version
.type __kernel_compiler_version, @function
.extern kernel_end
.type kernel_end, @function
.extern __kernel_name
.type __kernel_name, @function
.extern kernel_start
.type kernel_start, @function
.extern __kernel_version_format
.type __kernel_version_format, @function
.extern __kernel_version_lower
.type __kernel_version_lower, @function
.extern __kernel_version_major
.type __kernel_version_major, @function
.extern __kernel_version_minor
.type __kernel_version_minor, @function
.extern __kernel_version_suffix
.type __kernel_version_suffix, @function
.extern lastFrame
.type lastFrame, @function
.extern memcmp
.type memcmp, @function
.extern memcpy
.type memcpy, @function
.extern memmap_strings
.type memmap_strings, @function
.extern memmove
.type memmove, @function
.extern memset
.type memset, @function
.extern mmu_frame_clear
.type mmu_frame_clear, @function
.extern mmu_frame_set
.type mmu_frame_set, @function
.extern mmu_init
.type mmu_init, @function
.extern mmu_kernel_pagemap
.type mmu_kernel_pagemap, @function
.extern mmu_map_page
.type mmu_map_page, @function
.extern mmu_request_frame
.type mmu_request_frame, @function
.extern mmu_switch_pagemap
.type mmu_switch_pagemap, @function
.extern mmu_test_frame
.type mmu_test_frame, @function
.extern nframes
.type nframes, @function
.extern printf
.type printf, @function
.extern putchar_
.type putchar_, @function
.extern rodata_end
.type rodata_end, @function
.extern rodata_start
.type rodata_start, @function
.extern snprintf
.type snprintf, @function
.extern spinlock_acquire
.type spinlock_acquire, @function
.extern sprintf
.type sprintf, @function
.extern _start
.type _start, @function
.extern strlen
.type strlen, @function
.extern text_end
.type text_end, @function
.extern text_start
.type text_start, @function
.extern total_memory
.type total_memory, @function
.extern usedMemory
.type usedMemory, @function
.extern vfctprintf
.type vfctprintf, @function
.extern vprintf
.type vprintf, @function
.extern vsnprintf
.type vsnprintf, @function
.extern vsprintf
.type vsprintf, @function
.global kernel_symbols_start
kernel_symbols_start:

.quad bitmap
.asciz "bitmap"
.quad bytesOfBitmap
.asciz "bytesOfBitmap"
.quad data_end
.asciz "data_end"
.quad data_start
.asciz "data_start"
.quad fatal
.asciz "fatal"
.quad fctprintf
.asciz "fctprintf"
.quad freeMemory
.asciz "freeMemory"
.quad gdt
.asciz "gdt"
.quad gdt_init
.asciz "gdt_init"
.quad gdt_reload
.asciz "gdt_reload"
.quad hhdm_request
.asciz "hhdm_request"
.quad idt_init
.asciz "idt_init"
.quad idt_set_gate
.asciz "idt_set_gate"
.quad _isr0
.asciz "_isr0"
.quad _isr1
.asciz "_isr1"
.quad _isr10
.asciz "_isr10"
.quad _isr11
.asciz "_isr11"
.quad _isr12
.asciz "_isr12"
.quad _isr128
.asciz "_isr128"
.quad _isr13
.asciz "_isr13"
.quad _isr14
.asciz "_isr14"
.quad _isr15
.asciz "_isr15"
.quad _isr16
.asciz "_isr16"
.quad _isr17
.asciz "_isr17"
.quad _isr18
.asciz "_isr18"
.quad _isr19
.asciz "_isr19"
.quad _isr2
.asciz "_isr2"
.quad _isr20
.asciz "_isr20"
.quad _isr21
.asciz "_isr21"
.quad _isr22
.asciz "_isr22"
.quad _isr23
.asciz "_isr23"
.quad _isr24
.asciz "_isr24"
.quad _isr25
.asciz "_isr25"
.quad _isr26
.asciz "_isr26"
.quad _isr27
.asciz "_isr27"
.quad _isr28
.asciz "_isr28"
.quad _isr29
.asciz "_isr29"
.quad _isr3
.asciz "_isr3"
.quad _isr30
.asciz "_isr30"
.quad _isr31
.asciz "_isr31"
.quad _isr4
.asciz "_isr4"
.quad _isr5
.asciz "_isr5"
.quad _isr6
.asciz "_isr6"
.quad _isr7
.asciz "_isr7"
.quad _isr8
.asciz "_isr8"
.quad _isr9
.asciz "_isr9"
.quad isr_common
.asciz "isr_common"
.quad isr_handler
.asciz "isr_handler"
.quad isr_handler_inner
.asciz "isr_handler_inner"
.quad __kernel_arch
.asciz "__kernel_arch"
.quad __kernel_build_date
.asciz "__kernel_build_date"
.quad __kernel_build_time
.asciz "__kernel_build_time"
.quad __kernel_compiler_version
.asciz "__kernel_compiler_version"
.quad kernel_end
.asciz "kernel_end"
.quad __kernel_name
.asciz "__kernel_name"
.quad kernel_start
.asciz "kernel_start"
.quad __kernel_version_format
.asciz "__kernel_version_format"
.quad __kernel_version_lower
.asciz "__kernel_version_lower"
.quad __kernel_version_major
.asciz "__kernel_version_major"
.quad __kernel_version_minor
.asciz "__kernel_version_minor"
.quad __kernel_version_suffix
.asciz "__kernel_version_suffix"
.quad lastFrame
.asciz "lastFrame"
.quad memcmp
.asciz "memcmp"
.quad memcpy
.asciz "memcpy"
.quad memmap_strings
.asciz "memmap_strings"
.quad memmove
.asciz "memmove"
.quad memset
.asciz "memset"
.quad mmu_frame_clear
.asciz "mmu_frame_clear"
.quad mmu_frame_set
.asciz "mmu_frame_set"
.quad mmu_init
.asciz "mmu_init"
.quad mmu_kernel_pagemap
.asciz "mmu_kernel_pagemap"
.quad mmu_map_page
.asciz "mmu_map_page"
.quad mmu_request_frame
.asciz "mmu_request_frame"
.quad mmu_switch_pagemap
.asciz "mmu_switch_pagemap"
.quad mmu_test_frame
.asciz "mmu_test_frame"
.quad nframes
.asciz "nframes"
.quad printf
.asciz "printf"
.quad putchar_
.asciz "putchar_"
.quad rodata_end
.asciz "rodata_end"
.quad rodata_start
.asciz "rodata_start"
.quad snprintf
.asciz "snprintf"
.quad spinlock_acquire
.asciz "spinlock_acquire"
.quad sprintf
.asciz "sprintf"
.quad _start
.asciz "_start"
.quad strlen
.asciz "strlen"
.quad text_end
.asciz "text_end"
.quad text_start
.asciz "text_start"
.quad total_memory
.asciz "total_memory"
.quad usedMemory
.asciz "usedMemory"
.quad vfctprintf
.asciz "vfctprintf"
.quad vprintf
.asciz "vprintf"
.quad vsnprintf
.asciz "vsnprintf"
.quad vsprintf
.asciz "vsprintf"
.global kernel_symbols_end
kernel_symbols_end:
