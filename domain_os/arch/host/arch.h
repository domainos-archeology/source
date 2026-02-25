/*
 * arch/host/arch.h - Host/Testing Architecture Definitions
 *
 * Architecture definitions for building Domain/OS kernel code on the
 * host (development) machine. This enables unit testing without
 * requiring an M68K cross-compiler or target hardware.
 *
 * The host architecture:
 * - Uses native byte order (may be little-endian)
 * - Uses native pointer size (may be 64-bit)
 * - Provides no-op interrupt control
 * - Does not provide A5-relative global access
 *
 * When porting to a new real architecture, create arch/<arch>/arch.h
 * instead - this file is specifically for testing on the dev host.
 */

#ifndef ARCH_HOST_ARCH_H
#define ARCH_HOST_ARCH_H

/* Interrupt control (no-op stubs for testing) */
#include "arch/host/intr.h"

/*
 * Host memory model:
 *   - Byte order: native (use BE32_CONST/BE16_CONST for on-disk data)
 *   - Pointer size: native (4 or 8 bytes)
 *   - Alignment: native
 */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define ARCH_BIG_ENDIAN    1
#endif

/* Detect host pointer size */
#if __SIZEOF_POINTER__ == 8
#define ARCH_PTR_SIZE      8
#define ARCH_ALIGN_16      2
#define ARCH_ALIGN_32      4
#elif __SIZEOF_POINTER__ == 4
#define ARCH_PTR_SIZE      4
#define ARCH_ALIGN_16      2
#define ARCH_ALIGN_32      4
#else
#error "Unsupported host pointer size"
#endif

/*
 * A5 Global Data Pointer - NOT AVAILABLE on host
 *
 * Code that uses __A5_BASE() must be guarded with #if defined(ARCH_M68K)
 * or use the portable per-process data abstraction (see bead source-0i3).
 *
 * For testing, we provide a stub that returns NULL so that compilation
 * succeeds, but any runtime use will be caught by tests.
 */
static inline void *__A5_BASE(void) {
    return (void *)0;
}

#endif /* ARCH_HOST_ARCH_H */
