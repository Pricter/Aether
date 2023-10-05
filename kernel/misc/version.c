#include <kernel/version.h>

/**
 * 0.0.1: Start development
 * 0.0.2: Name change to Aether and scheduler dones
*/

#define STR(x) #x
#define STRSTR(x) STR(x)

/* Please dont change */
const char *__kernel_name = "Aether";

const char *__kernel_version_format = "%d.%d.%d-%s";

/* Version numbers */
int __kernel_version_major = 0;
int __kernel_version_minor = 0;
int __kernel_version_lower = 2;

/* Kernel build suffix, which doesn't necessarily
 * mean anything, but can be used to distinguish
 * between different features included while
 * building multiple kernels. */
#ifdef KERNEL_GIT_TAG
# define KERNEL_VERSION_SUFFIX STRSTR(KERNEL_GIT_TAG)
#else
# define KERNEL_VERSION_SUFFIX "r"
#endif
const char * __kernel_version_suffix = KERNEL_VERSION_SUFFIX;

/* Build architecture */
const char * __kernel_arch = STRSTR(KERNEL_ARCH);

/* Rebuild from clean to reset these. */
const char * __kernel_build_date = __DATE__;
const char * __kernel_build_time = __TIME__;

#if (defined(__GNUC__) || defined(__GNUG__)) && !(defined(__clang__) || defined(__INTEL_COMPILER))
# define COMPILER_VERSION "gcc " __VERSION__
#elif (defined(__clang__))
# define COMPILER_VERSION "clang " __clang_version__
#else
# define COMPILER_VERSION "unknown-compiler how-did-you-do-that"
#endif

const char * __kernel_compiler_version = COMPILER_VERSION;