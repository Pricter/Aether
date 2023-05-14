#include <stdint.h>
#include <stddef.h>
#include <kernel/mmu.h>
#include <limine.h>

static volatile struct limine_kernel_file_request kfile_request = {
	.id = LIMINE_KERNEL_FILE_REQUEST,
	.revision = 0,
};
struct limine_file *kernel_file = NULL;
void* kfile_address = NULL;

void symbols_init(void) {
	kernel_file = kfile_request.response->kernel_file;
	kfile_address = kernel_file->address;

	printf("symbols: kernel file at %p\n", kfile_address);
}