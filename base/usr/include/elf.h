#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Elf types 16bit */
typedef uint16_t Elf32_Half;
typedef uint16_t Elf64_Half;

/* Elf types signed and unsigned 32bit */
typedef uint32_t Elf32_Word;
typedef int32_t  Elf32_Sword;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;

/* Elf types signed and unsigned 64bit */
typedef uint64_t Elf32_Xword;
typedef int64_t  Elf32_Sxword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;

/* Elf type addresses */
typedef uint32_t Elf32_Addr;
typedef uint64_t Elf64_Addr;

/* Elf types file offsets */
typedef uint32_t Elf32_Off;
typedef uint64_t Elf64_Off;

/* Elf type section indices 16bit */
typedef uint16_t Elf32_Section;
typedef uint16_t Elf64_Section;

/* Elf type version symbol information */
typedef uint32_t Elf32_Versym;
typedef uint64_t Elf64_Versym;

#define EI_NIDENT 16

typedef struct {
	/**
	 * The initial bytes mark the file as an object file and provide machine
	 * independent data with which to decode and interpret the file's contents
	*/
	unsigned char e_ident[EI_NIDENT];

	/* This member identifies the object file type */
	Elf32_Half e_type;
	
	/* This member's value specifies the required architecture for a file */
	Elf32_Half e_machine;

	/* Identifies the object file version */
	Elf32_Word e_version;

	/**
	 * Gives the virtual address to which the system first transfers
	 * control, thus starting the process. If the file has no associated
	 * entry point, this member holds zero.
	*/
	Elf32_Addr e_entry;

	/**
	 * Holds the program header table's file offset in bytes, i fthe program
	 * has no header table, this member holds zero.
	 */
	Elf32_Off  e_phoff;

	/**
	 * Holds the section header table's offset in bytes. If the file
	 * has no section header, then this member holds zero.
	 */
	Elf32_Off  e_shoff;
	
	/** 
	 * Holds processor-specific flags associated with the file.
	 * Flag names take the form EF_machine_flag
	*/
	Elf32_Word e_flags;

	/* Holds the ELF header's size in bytes */
	Elf32_Half e_ehsize;

	/* Holds the size in bytes of one entry in the file's program header table */
	Elf32_Half e_phentsize;

	/**
	 * This member holds the number of entries in the program header table. Thus
	 * the product of e_phentsize and e_phnum gives the table's size in bytes. If
	 * a file has no program header table, e_phnum holds the value zero.
	*/
	Elf32_Half e_phnum;

	/* Holds a section header's size in bytes */
	Elf32_Half e_shentsize;

	/**
	 * This member holds the number of entries in the section header table. Thus
	 * the product of e_shentsize and e_shnum gives the section header table's size
	 * in bytes. If a file has no section header, e_shnum holds the value zero.
	 * 
	 * If the number of sections is greater than or equal to SHN_LORESERVE(0xff00),
	 * ths member has the value zero and the actual number of section header tables is
	 * contained in the sh_size field of the section header at index 0. (Otherwise,
	 * the sh_size member of the initial entry contains 0)
	*/
	Elf32_Half e_shnum;

	/**
	 * This member holds the section header table index of the entry
	 * associated with the section name string table. If the file has
	 * no section name string table, this member holds the value
	 * SHN_UNDEF
	*/
	Elf32_Half e_shstrndx;
} Elf32_Ehdr;

typedef struct {
	/**
	 * The initial bytes mark the file as an object file and provide machine
	 * independent data with which to decode and interpret the file's contents
	*/
	unsigned char e_ident[EI_NIDENT];

	/* This member identifies the object file type */
	Elf64_Half e_type;

	/* This member's value specifies the required architecture for a file */
	Elf64_Half e_machine;

	/* Identifies the object file version */
	Elf64_Word e_version;

	/**
	 * Gives the virtual address to which the system first transfers
	 * control, thus starting the process. If the file has no associated
	 * entry point, this member holds zero.
	*/
	Elf64_Addr e_entry;

	/**
	 * Holds the program header table's file offset in bytes, if the program
	 * has no header table, this member holds zero.
	*/
	Elf64_Off  e_phoff;

	/**
	 * Holds the section header table's offset in bytes. If the file
	 * has no section header, then this member holds zero.
	*/
	Elf64_Off  e_shoff;

	/** 
	 * Holds processor-specific flags associated with the file.
	 * Flag names take the form EF_machine_flag
	*/
	Elf64_Word e_flags;

	/* Holds the ELF header's size in bytes */
	Elf64_Half e_ehsize;

	/* Holds the size in bytes of one entry in the file's program header table */
	Elf64_Half e_phentsize;

	/**
	 * This member holds the number of entries in the program header table. Thus
	 * the product of e_phentsize and e_phnum gives the table's size in bytes. If
	 * a file has no program header table, e_phnum holds the value zero.
	*/
	Elf64_Half e_phnum;

	/* Holds a section header's size in bytes */
	Elf64_Half e_shentsize;

	/**
	 * This member holds the number of entries in the section header table. Thus
	 * the product of e_shentsize and e_shnum gives the section header table's size
	 * in bytes. If a file has no section header, e_shnum holds the value zero.
	 * 
	 * If the number of sections is greater than or equal to SHN_LORESERVE(0xff00),
	 * ths member has the value zero and the actual number of section header tables is
	 * contained in the sh_size field of the section header at index 0. (Otherwise,
	 * the sh_size member of the initial entry contains 0)
	*/
	Elf64_Half e_shnum;

	/**
	 * This member holds the section header table index of the entry
	 * associated with the section name string table. If the file has
	 * no section name string table, this member holds the value
	 * SHN_UNDEF
	*/
	Elf64_Half e_shstrndx;
} Elf64_Ehdr;

/* Values of the e_type field of the Ehdr */
#define ET_NONE 0 /* No file type */
#define ET_REL 1 /* Relocatable file */
#define ET_EXEC 2 /* Executable file */
#define ET_DYN 3 /* Shared object file */
#define ET_CORE 4 /* Core file */
#define ET_LOOS 0xfe00 /* Operating system specifc */
#define ET_HIOS 0xfeff /* Operating system specific */
#define ET_LOPROC 0xff00 /* Processor-specific */
#define ET_HIPROC 0xffff /* Processor-specific */

/* Values of the e_machine field of the Ehdr */
#define EM_NONE 0 /* No machine */
#define EM_M32 1 /* AT&T WE 32100 */
#define EM_SPARC 2 /* SPARC */
#define EM_386 3 /* Intel 80386 */
#define EM_68K 4 /* Motorola 68000 */
#define EM_88K 5 /* Motorola 88000 */
/* 6 Is reserved for future, previously it was EM_486 */
#define EM_860 7 /* Intel 80860 */
#define EM_MIPS 8 /* MIPS I Architecture */
#define EM_S370 9 /* IDM System/370 Processor */
#define EM_MIPS_RS3_LE 10 /* MIPS RS3000 Little-endian */
/* 11-14 are reserved for future */
#define EM_PARISC 15 /* Hewlett-Packard PA_RISC */
/* 16 reserved for future */
#define EM_VPP400 17 /* Fujitsu VPP500 */
#define EM_SPARC32PLUS 18 /* Enhanced instruction set SPARC */
#define EM_960 19 /* Intel 80960 */
#define EM_PPC 20 /* PowerPC */
#define EM_PPC64 21 /* 64-bit PowerPC */
#define EM_S390 22 /* IBM System/390 Processor */
/* 23-35 Reserved for future */
#define EM_V800 36 /* NEC V800 */
#define EM_FR20 37 /* Fujitsu FR20 */
#define EM_RH32 38 /* TRW RH-32 */
#define EM_RCE 39 /* Motorola RCE */
#define EM_ARM 40 /* Advanced RISC Machines ARM */
#define EM_ALPHA 41 /* Digital Alpha */
#define EM_SH 42 /* Hitachi SH */
#define EM_SPARCV9 43 /* SPARC Version 9 */
#define EM_TRICORE 44 /* Siemens TriCore embedded processor */
#define EM_ARC 45 /* Argonaut RISC Core, Argonaut Technologies Inc. */
#define EM_H8_300 46 /* Hitachi H8/300 */
#define EM_H8_300H 47 /* Hitachi H8/300H */
#define EM_H8S 48 /* Hitachi H8S */
#define EM_H8_500 49 /* Hitachi H8/500 */
#define EM_IA_64 50 /* Intel IA-64 processor architecture */
#define EM_MIPS_X 51 /* Stanford MIPS-X */
#define EM_COLDFIRE 52 /* Motorola ColdFire */
#define EM_68HC12 53 /* Motorola M68HC12 */
#define EM_MMA 54 /* Fujitsu MMA Multimedia Accelerator */
#define EM_PCP 55 /* Siemens PCP */
#define EM_NCPU 56 /* Sony nCPU embedded RISC processor */
#define EM_NDR1 57 /* Denso NDR1 microprocessor */
#define EM_STARCORE 58 /* Motorola Star*Core processor */
#define EM_ME16 59 /* Toyota ME16 processor */
#define EM_ST100 60 /* STMicroelectronics ST100 processor */
#define EM_TINYJ 61 /* Advanced Logic Corp. TinyJ embedded processor family */
#define EM_X86_64 62 /* AMD x86-64 architecture */
#define EM_PDSP 63 /* Sony DSP Processor */
#define EM_PDP10 64 /* Digital Equipment Corp. PDP-10 */
#define EM_PDP11 65 /* Digital Equipment Corp. PDP-11 */
#define EM_FX66 66 /* Siemens FX66 microcontroller */
#define EM_ST9PLUS 67 /* STMicroelectronics ST9+8/16 bit microcontroller */
#define EM_ST7 68 /* STMiroelectronics ST7 8-bit microcontroller */
#define EM_689HC16 69 /* Motorola MC68C16 Microcontroller */
#define EM_689HC11 70 /* Motorola MC68C11 Microcontroller */
#define EM_689HC08 71 /* Motorola MC68C08 Microcontroller */
#define EM_689HC05 72 /* Motorola MC68C05 Microcontroller */
#define EM_SVX 73 /* Silicon Graphics SVx */
#define EM_ST19 74 /* STMicroelectronics ST19 8-bit microcontroller */
#define EM_VAX 75 /* Digital VAX */
#define EM_CRIS 76 /* Axis Communications 32-bit embedded processor */
#define EM_JAVELIN 77 /* Infineon Technologies 32-bit embedded processor */
#define EM_FIREPATH 78 /* Element 14 64-bit DSP Processor */
#define EM_ZSP 79 /* LSI Logic 16-bit DSP Processor */
#define EM_MMIX 80 /* Donald Knuth's educational 64-bit processor */
#define EM_HUANY 81 /* Harvard University machine-independent object files */
#define EM_PRISM 82 /* SiTera Prism */
#define EM_AVR 83 /* Atmel AVR 8-bit microcontroller */
#define EM_FR30 84 /* Fujitsu FR30 */
#define EM_D10V 85 /* Mitsubishi D10V */
#define EM_D30V 86 /* Mitsubishi D30V */
#define EM_V850 87 /* NEC v850 */
#define EM_M32R 88 /* Mitsubishi M32R */
#define EM_MN10300 89 /* Matsushita MN10300 */
#define EM_MN10200 90 /* Matsushita MN10200 */
#define EM_PJ 91 /* picoJava */
#define EM_OPENRISC 92 /* OpenRISC 32-bit embedded processor */
#define EM_ARC_A5 93 /* ARC Cores Tangent-A5 */
#define EM_XTENSA 94 /* Tensilica Xtensa Architecture */
#define EM_VIDEOCORE 95 /* Alphamosaic VideoCore processor */
#define EM_TMM_GPP 96 /* Thompson Multimedia General Purpose Processor */
#define EM_NS32K 97 /* National Semiconductor 3200 series */
#define EM_TPC 98 /* Tenor Network TPC processor */
#define EM_SNP1K 99 /* Trebia SNP 1000 processor */
#define EM_ST200 100 /* STMicroelectronics (www.st.com) ST200 microcontroller */

/* Values of e_version */
#define EV_NONE 0 /* Invalid version */
#define EV_CURRENT 1 /* Current version */

/* Elf identification */
#define EI_MAG0 0 /* File identification */
#define EI_MAG1 1 /* File identification */
#define EI_MAG2 2 /* File identification */
#define EI_MAG3 3 /* File identification */
#define EI_CLASS 4 /* File class */
#define EI_DATA 5 /* Data encoding */
#define EI_VERSION 6 /* File version */
#define EI_OSABI 7 /* Operating system/ABI identification */
#define EI_ABIVERSION 8 /* ABI version */
#define EI_PAD 9 /* Start of padding bytes */

#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ELFCLASSNONE 0 /* Invalid class */
#define ELFCLASS32 1 /* 32-bit objects */
#define ELFCLASS64 2 /* 64-bit objects */

#define ELFDATANONE 0 /* Invalid data encoding */
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

/* WIP */

#ifdef __cplusplus
}
#endif