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

/**
 * ELF Headers:
 * 
 * - e_ident: The initial bytes mark the file as an object file and provide machine
 *   		  independent data with which to decode and interpret the file's contents
 * - e_type: This member identifies the object file type
 * - e_machine: This member's value specifies the required architecture for a file
 * - e_version: Identifies the object file version
 * - e_entry: Gives the virtual address to which the system first transfers
 * 			  control, thus starting the process. If the file has no associated
 * 			  entry point, this member holds zero.
 * - e_phoff: Holds the program header table's file offset in bytes, i fthe program
 * 			   has no header table, this member holds zero.
 * - e_shoff: Holds the section header table's offset in bytes. If the file
 * 			  has no section header, then this member holds zero.
 * - e_flags: Holds processor-specific flags associated with the file.
 * 			  Flag names take the form EF_machine_flag
 * - e_ehsize: Holds the ELF header's size in bytes
 * - e_phentsize: Holds the size in bytes of one entry in the file's program header table
 * - e_phnum: This member holds the number of entries in the program header table. Thus
 * 			  the product of e_phentsize and e_phnum gives the table's size in bytes. If
 * 			  a file has no program header table, e_phnum holds the value zero.
 * - e_shentsize: Holds a section header's size in bytes
 * - e_shnum: This member holds the number of entries in the section header table. Thus
 * 			  the product of e_shentsize and e_shnum gives the section header table's size
 * 			  in bytes. If a file has no section header, e_shnum holds the value zero.
 * 			  
 * 			  If the number of sections is greater than or equal to SHN_LORESERVE(0xff00),
 * 			  ths member has the value zero and the actual number of section header tables is
 * 			  contained in the sh_size field of the section header at index 0. (Otherwise,
 * 			  the sh_size member of the initial entry contains 0)
 * - e_shstrndx: This member holds the section header table index of the entry
 *			  associated with the section name string table. If the file has
 *			  no section name string table, this member holds the value SHN_UNDEF
*/

/* 32bit elf header */
typedef struct {
	unsigned char e_ident[EI_NIDENT];
	Elf32_Half e_type;
	Elf32_Half e_machine;
	Elf32_Word e_version;
	Elf32_Addr e_entry;
	Elf32_Off  e_phoff;
	Elf32_Off  e_shoff;
	Elf32_Word e_flags;
	Elf32_Half e_ehsize;
	Elf32_Half e_phentsize;
	Elf32_Half e_phnum;
	Elf32_Half e_shentsize;
	Elf32_Half e_shnum;
	Elf32_Half e_shstrndx;
} Elf32_Ehdr;

/* 64-bit elf header */
typedef struct {
	unsigned char e_ident[EI_NIDENT];
	Elf64_Half e_type;
	Elf64_Half e_machine;
	Elf64_Word e_version;
	Elf64_Addr e_entry;
	Elf64_Off  e_phoff;
	Elf64_Off  e_shoff;
	Elf64_Word e_flags;
	Elf64_Half e_ehsize;
	Elf64_Half e_phentsize;
	Elf64_Half e_phnum;
	Elf64_Half e_shentsize;
	Elf64_Half e_shnum;
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
#define ELFDATA2LSB 1 /* 2's compliment, little endian */
#define ELFDATA2MSB 2 /* 2's compliment, big endian */

#define ELFOSABI_NONE 0 /* Unix System V ABI */
#define ELFOSABI_SYSV 0 /* Alias */
#define ELFOSABI_HPUX 1 /* HP-UX */
#define ELFOSABI_NETBSD 2 /* NetBSD */
#define ELFOSABI_LINUX 3 /* Linux */
#define ELFOSABI_SOLARIS 6 /* Sun solaris */
#define ELFOSABI_AIX 7 /* IBM AIX */
#define ELFOSABI_IRIX 8 /* SGI Irix */
#define ELFOSABI_FREEBSD 9 /* FreeBSD */
#define ELFOSABI_TRU64 10 /* Compaq TRU64 UNIX */
#define ELFOSABI_MODESTO 11 /* Novell Modesto */
#define ELFOSABI_OPENBSD 12 /* OpenBSD */
#define ELFOSABI_ARM 97 /* ARM */
#define ELFOSABI_STANDALONE 255 /* Standalone (embedded) application */

/* Special section indexes */
#define SHN_UNDEF 0 /* Marks unidentified, missing, irrelevant, or otherwise meaningless section reference */
#define SHN_LORESERVE 0xff00 /* Specifies the lower bound of the range of reserved indexes */
#define SHN_LOPROC 0xff00 /* Reserved for processor specific semantics */
#define SHN_HIPROC 0xff1f /* Reserved for processor specific semantics */
#define SHN_LOOS 0xff20 /* Reserved for operating system specific sematicss */
#define SHN_HIOS 0xff3f /* Reserved for operating system specific semantics */
/**
 * Specifies the absolute values for corresponding reference,
 * symbols defined relative to SHN_ABS have absolute values
 * and are not affected by relocation
*/
#define SHN_ABS 0xfff1
#define SHN_COMMON 0xfff2 /* Symbols defined relative to this section are common symbols */
/** 
 * Escape value, indicates that the actual section header is too
 * large to fit in the containing field and is to be found
 * in another location (specific to the structure where it appears)
*/
#define SHN_XINDEX 0xffff
/**
 * Specifies the upper bound of the range of reserved indexes. The system reserved
 * indexes between SHN_LORESERVE and SHN_HIRESERVE inclusive; the values do not
 * reference the section header table. The section header table does not contain entries
 * for the reserved indexes
*/
#define SHN_HIRESERVE 0xffff

/**
 * Section headers
 * 
 * - sh_name: Name of the section. Its value is an index into the section header
 * 			  string table section giving the location of a null-terminated string
 * - sh_type: Categorizes the section's contents and semantics
 * - sh_flags: Sections support 1-bit flags that describe miscellaneous attributes
 * - sh_addr: If the section will appear in the memory image of a process, this
 * 			  member gives the address at which the section's fist byte should
 * 			  reside. Otherwise, the member contains 0.
 * - sh_offset: This member's value gives the byte offset from the beginning of
 * 			  the file to the first byte in the section. One section type
 * 			  SHT_NOBITS ocupies no space in the file, and its sh_offset member
 * 			  locates the conceptual placement in the file.
 * - sh_size: This member gives the section's size in bytes. Unless the sectoin type
 * 			  is SHT_NOBITS, the section occupies sh_size bytes in the file. A section
 * 			  of type SHT_NOBITS may have a non-zero size, but it occupies no space
 * - sh_link: This member holds a section header table index link, whose interpretation
 * 			  depends on the section type
 * - sh_info: This member holds extra information, whose interpretation depends on the
 * 			  section type. If the sh_flags field for this section include the attribute
 * 			  SHF_INFO_LINK, then this member represents a section header table index.
 * - sh_addralign: Some sections have address alignment constraints. The value of
 * 			  sh_addr must be congruent to 0, modulo the value of sh_addralign.
 * 			  Currently, only 0 and positive integral powers of two are allowed.
 * 			  Values 0 and 1 mean the section has no alignment constraints
 * - sh_entsize: Some sections hold a table of fixed-size entries, such as a
 * 			  symbol table. For such a section, this member gives the size in bytes
 * 			  of each entry. The members contains 0 if the section does not hold a
 * 			  table of fixed-size entries.	
*/

/* 32-bit section header */
typedef struct {
	Elf32_Word sh_name;
	Elf32_Word sh_type;
	Elf32_Word sh_flags;
	Elf32_Addr sh_addr;
	Elf32_Off  sh_offset;
	Elf32_Word sh_size;
	Elf32_Word sh_link;
	Elf32_Word sh_info;
	Elf32_Word sh_addralign;
	Elf32_Word sh_entsize;
} Elf32_Shdr;

/* 64-bit section header */
typedef struct {
	Elf64_Word sh_name;
	Elf64_Word sh_type;
	Elf64_Xword sh_flags;
	Elf64_Addr sh_addr;
	Elf64_Off  sh_offset;
	Elf64_Xword sh_size;
	Elf64_Word sh_link;
	Elf64_Word sh_info;
	Elf64_Word sh_addralign;
	Elf64_Word sh_entsize;
} Elf64_Shdr;

/**
 * ELF Symbol
 * 
 * - st_name: Index into the object file's symbol string table, which
 * 			  holds the charactrer representations of symbol names. If
 * 			  the value is non-zero, it represents a string table index
 * 			  that gives the symbol name. Otherwise the symbol table
 * 			  entry has no name.
 * - st_value: Gives the value of associated symbol. Depending on the
 *			  context, this may be an absolute value, an address, and
 * 			  so on
 * - st_info: This member specifies the symbol's type and binding
 * 			  attributes.
 * - st_other: Specifies a symbol's visibility. Other bits contain 0 and
 * 			  have no defined meaning.
 * - st_shndx: Every symbol table entry is defined in relation to some
 * 			  section. This member holds the relevant section header
 * 			  table index. Some section indexes indicate special meaning
*/

/* 32-bit ELF symbol */
typedef struct {
	Elf32_Word st_name;
	Elf32_Addr st_value;
	Elf32_Word st_size;
	unsigned char st_info;
	unsigned char st_other;
	Elf32_Half st_shndx;
} Elf32_Sym;

/* 64-bit ELF symbol */
typedef struct {
	Elf64_Word st_name;
	unsigned char st_info;
	unsigned char st_other;
	Elf64_Half st_shndx;
	Elf64_Addr st_value;
	Elf64_Xword st_size;
} Elf64_Sym;

/* Macros for extracting info from st_info */
#define ELF32_ST_BIND(i) ((i) >> 4)
#define ELF32_ST_TYPE(i) ((i) & 0xf)
#define ELF32_ST_INFO(b, t) (((b) << 4) + ((t) & 0xf))

#define ELF64_ST_BIND(i) ((i) >> 4)
#define ELF64_ST_TYPE(i) ((i) & 0xf)
#define ELF64_ST_INFO(b, t) (((b) << 4) + ((t) & 0xf))

/* Macros for getting the visibility from st_other */
#define ELF32_ST_VISIBILITY(o) ((o) & 0x3)
#define ELF64_ST_VISIBILITY(o) ((o) & 0x3)

/* Legal values for st_other visibility */

/**
 * Local symbols are not visible outside the object file containing their
 * definition. Local symbols of the name may exist in multiple files
 * without interfering with each other.
*/
#define STB_LOCAL 0

/**
 * Global symbols are visible to all object files being combines.
 * One file's definition of a global symbol will satisfy another
 * file's undefined reference to the same global symbol.
*/
#define STB_GLOBAL 1

/**
 * Weak symbols resemble global symbols, but their definitions have
 * lower precedence.
*/
#define STB_WEAK 2

/* Reserved for operating system specific semantics */
#define STB_LOOS 10
#define STB_HIOS 12

/* Reserved for processor specific semantics */
#define STB_LOPROC 13
#define STB_HIPROC 15

/* Legal values for symbol types */

/* The symbol's type is not specified */
#define STT_NOTYPE 0

/* The symbol is associated with a data object, such as a variable */
#define STT_OBJECT 1

/* The symbol is associated with a function or other executable code */
#define STT_FUNC 2

/* The symbol is associated with a section */
#define STT_SECTION 3

/**
 * Conventionally the symbol's name gives the name of the source file
 * associated with the object file. A file symbol has STB_LOCAL
 * binding, its section index is SHN_ABS, and it precedes the other
 * STB_LOCAL symbols for the file, if it is present
*/
#define STT_FILE 4

/* The symbol labels an uninitalized common block */
#define STT_COMMON 5

/**
 * The symbol specifies a Thread-Local Storage entity. When defined,
 * it gives the assigned offset for the symbol, not the actual
 * address. Symbols of tyupe STT_TLS can be referenced by only
 * special thread-local storage relocations and thread-local
 * storage relocations can only reference symbols with type STT_TLS.
 * Implementation need not support thread-local storage
*/
#define STT_TLS 6

/* Reserved for operating system specific semantics */
#define STT_LOOS 10
#define STT_HIOS 12

/* Reserved for processor specific semantics */
#define STT_LOPROC 13
#define STT_HIPROC 15

/* Legal values for symbol visibility */

// TODO: Document
#define STV_DEFAULT 0
#define STV_INTERNAL 1
#define STV_HIDDEN 2
#define STV_PROTECTED 3

/* WIP */

#ifdef __cplusplus
}
#endif