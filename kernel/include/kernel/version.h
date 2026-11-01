#pragma once
#include <kernel/build_version.h>

/* Central identity and release policy. Major stays zero during initial development. */
#define OS_NAME "Aether"
#define OS_NODE_NAME "Aether"
#define OS_MACHINE "aarch64"
#define KERNEL_VERSION_MAJOR 0
#define KERNEL_VERSION_MINOR 1
#define VERSION_STRINGIFY_INNER(value) #value
#define VERSION_STRINGIFY(value) VERSION_STRINGIFY_INNER(value)
#define KERNEL_VERSION_BASE                                                                        \
    VERSION_STRINGIFY(KERNEL_VERSION_MAJOR) "." VERSION_STRINGIFY(KERNEL_VERSION_MINOR)
#define KERNEL_VERSION KERNEL_VERSION_BASE "-" KERNEL_GIT_REVISION "-" KERNEL_BUILD_TIMESTAMP
