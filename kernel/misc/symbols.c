/**
 * symbols.c: Gets symbols defined in the kernel, basically, the kernel indexes itself.
 * 
 * Used for stacktrace to know the names of functions at addresses.
 */

#include <stdint.h>
#include <stddef.h>
#include <kernel/mmu.h>
#include <limine.h>
#include <kernel/kprintf.h>
#include <elf.h>
#include <memory.h>
#include <kernel/symbols.h>
#include <string.h>
#include <kernel/macros.h>

extern void fatal(void);

/* Request limine for kernel file to parse symbols */
__attribute__((used, section(".requests")))
static volatile struct limine_kernel_file_request kfile_request = {
	.id = LIMINE_KERNEL_FILE_REQUEST,
	.revision = 0,
};
struct limine_file *kernel_file = NULL;
void* kfile_address = NULL;

/* Some needed variables */
Elf64_Shdr* symbol_table_hdr = NULL;
Elf64_Sym* symbol_table = NULL;
char* symbol_string_table = NULL;
uint64_t symbol_count = 0;
uint64_t function_count = 0;

void* debug_info_start = NULL;

/* Store out functions */
ksym_func_t* function_table = NULL;

/* Macros to make tghe code cleaner */
#define GET_NAME(s, n) ((const char*)((uintptr_t)s + n))
#define JMP_BYTES(f, o) ((uintptr_t)f + o)

/* Not the end of the world if we dont have symbols */
static bool symbols_present = false;

/* Indexes the kernel's ELF file to get funtion names */
/* TODO: Move ELF parsing into a seperate file dedicated to ELF parsing */
void __init symbols_init(void) {
	kfile_address = kfile_request.response->kernel_file->address;
	Elf64_Ehdr* hdr = kfile_address;

	/* Set startings addresses for Section headers, string header */
	Elf64_Shdr* shdr = (Elf64_Shdr*)JMP_BYTES(hdr, hdr->e_shoff);
	Elf64_Shdr* string_shdr = (Elf64_Shdr*) JMP_BYTES(kfile_address,
		(hdr->e_shentsize * hdr->e_shstrndx + hdr->e_shoff));
	char* string_table = (char*)((uintptr_t)kfile_address + string_shdr->sh_offset);

	/* Find .symtab and .strtab sections */
	for(uint64_t i = 0; i < hdr->e_shnum; i++) {
		const char* name = (const char*)JMP_BYTES(string_table, shdr->sh_name);
		if(!strcmp(name, ".symtab")) {
			symbol_table_hdr = shdr;
			symbol_table = (Elf64_Sym*)JMP_BYTES(kfile_address, shdr->sh_offset);
			symbol_count = shdr->sh_size / sizeof(Elf64_Sym);
		}
		if(!strcmp(name, ".strtab")) symbol_string_table = (char*)JMP_BYTES(kfile_address, shdr->sh_offset);
		if(!strcmp(name, ".debug_info")) debug_info_start = (void*)JMP_BYTES(kfile_address, shdr->sh_offset);
		shdr = (Elf64_Shdr*)JMP_BYTES(shdr, hdr->e_shentsize);
	}

	/* Print if we dont have symbols and return (nothing left) */
	if(symbol_table == NULL || symbol_string_table == NULL) {
		kprintf("The kernel file does not contain `.symtab` and `.strtab` sections!\n");
		symbols_present = false;
		return;
	}
	
	/* At this point we are certain to have symbols */
	symbols_present = true;

	/* Array of all the functions */
	function_table = malloc(sizeof(ksym_func_t) * (symbol_count - 1));

	/* Add all the functions to the array, start from 1 (ignore the first null entry) */
	for(uint64_t i = 1; i < symbol_count - 1; i++) {
		/* Only add entry if symbol is a function */
		if(ELF64_ST_TYPE(symbol_table[i].st_info) == STT_FUNC) {
			function_table[function_count] = (ksym_func_t){
				.name = (char*)GET_NAME(symbol_string_table, symbol_table[i].st_name),
				.addr = symbol_table[i].st_value
			};
			function_table[function_count - 1].next = &function_table[function_count];
			function_count++;
		}
	}

	kprintf("symbols: Found %lu functions in kernel\n", function_count);
}

/**
 * symbols_search
 * 
 * Search a symbol closest lower than address
 * 
 * @param address The address to match
*/
ksym_func_t* symbols_search(uintptr_t address) {
	if(symbols_present == false) return NULL;
    ksym_func_t* closest_symbol = (ksym_func_t*)((uint64_t)symbol_table);

	for(uint64_t i = 0; i < function_count; i++) {
		if(function_table[i].addr <= address && function_table[i].addr > closest_symbol->addr) {
			closest_symbol = &function_table[i];
		}
	}

    return closest_symbol;
}

/* Get stacktrace */
void stacktrace(void) {
	stack_frame_t* stack;
	asm volatile ("movq %%rbp, %0" : "=r"(stack));
	kprintf("Stack trace:\n");
	while(stack->rip != 0) {
		ksym_func_t* func = symbols_search(stack->rip);
		char* name = func->name;
		kprintf("\t<%p> [%s + 0x%lx]\n", stack->rip, (name == NULL) ? "[unknown]" : name, stack->rip - func->addr);
		stack = stack->rbp;
	}
}