#pragma once
#include <kernel/exception.h>

/* AArch64 ABI: x8 number, x0..x5 arguments, signed x0 result. The frame
 * belongs to the caller's kernel stack and is never retained by a handler.
 * User addresses must pass checked copies before reaching filesystem code. */
long arm64_syscall_dispatch(arm64_exception_frame_t *frame);
