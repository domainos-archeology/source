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

#include <stdint.h>    /* uintptr_t, for ARCH_VA_TO_PTR */

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
 * ARCH_SPIN_TICK() - one iteration of a hardware-timing busy-wait loop.
 *
 * Domain/OS meets device setup/hold times by counting down a register in a
 * tight loop (cal_$delay at 0x00E81756, time_$read_cal_delay at 0x00E2AF58).
 * Those loop bodies have no other side effect, so a C compiler is free to
 * delete them entirely.  This macro expands to an empty volatile asm with a
 * memory clobber: the compiler must keep the loop and run it exactly the
 * requested number of times, and it emits no instructions of its own.
 */
#define ARCH_SPIN_TICK() __asm__ __volatile__("" ::: "memory")

/*
 * ARCH_VA_TO_PTR / ARCH_PTR_TO_VA - target virtual addresses
 *
 * See arch/m68k/arch.h.  A 64-bit host pointer does not fit in the uint32_t
 * fields the binary uses for virtual addresses, so a test that wants the
 * code under test to dereference such a field points ARCH_HOST_VA_BASE at
 * its own arena and stores offsets into that arena in the field.
 *
 * The default base of zero makes both macros plain casts, which is what
 * every test that does not set it already assumes.  Host builds are one
 * translation unit per test program, so a file-static is enough.
 */
static uintptr_t ARCH_HOST_VA_BASE = 0;

static inline void *ARCH_VA_TO_PTR_FN(uint32_t va)
{
    /* virtual address zero is nil on the target, and must stay nil here */
    return va ? (void *)(ARCH_HOST_VA_BASE + (uintptr_t)va) : (void *)0;
}

static inline uint32_t ARCH_PTR_TO_VA_FN(const void *p)
{
    return p ? (uint32_t)((uintptr_t)p - ARCH_HOST_VA_BASE) : 0u;
}

#define ARCH_VA_TO_PTR(va) ARCH_VA_TO_PTR_FN((uint32_t)(va))
#define ARCH_PTR_TO_VA(p)  ARCH_PTR_TO_VA_FN((const void *)(p))

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
