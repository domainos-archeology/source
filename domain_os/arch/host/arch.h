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
 * ARCH_VECTOR / ARCH_AUTOVECTOR - CPU exception vector table entries
 *
 * See arch/m68k/arch.h.  There is no vector table on the host, so the entries
 * are elements of an ordinary array a test program defines for itself:
 *
 *   void *arch_$vector_table[ARCH_VECTOR_COUNT];
 *
 * (the same arrangement as arch/host/intr.h's __host_intr_disable_count).
 */
#define ARCH_VECTOR_COUNT      256
extern void *arch_$vector_table[ARCH_VECTOR_COUNT];
#define ARCH_VECTOR(n)         (arch_$vector_table[(n)])
#define ARCH_AUTOVECTOR(level) ARCH_VECTOR(24u + (level))

/*
 * MODULE_DATA_DEFINE / MODULE_DATA_DEFINE_INIT - Pascal module data blocks
 * (see arch/m68k/arch.h; MODULE_DATA_DECLARE and MODULE_DATA_ADDR are the
 * shared ones in arch/arch.h)
 *
 * On the host a block is an ordinary object wherever the compiler puts it,
 * zero-initialised unless the defining _data.c supplies the image's
 * contents; the original address lives only in the declaration's constant.
 * The evenness check and the check against the declaration are the same as
 * the target's, so a bad address fails on either build.
 */
#define MODULE_DATA_CHECK_ADDR_(name, addr)                                  \
    _Static_assert(((addr) & 1u) == 0u,                                      \
                   #name ": module data address must be even");              \
    _Static_assert((addr) == moddata_addr_##name,                            \
                   #name ": address differs from its MODULE_DATA_DECLARE")

#define MODULE_DATA_DEFINE(T, name, addr)                                    \
    MODULE_DATA_CHECK_ADDR_(name, addr);                                     \
    T name

#define MODULE_DATA_DEFINE_INIT(T, name, addr, ...)                          \
    MODULE_DATA_CHECK_ADDR_(name, addr);                                     \
    T name = __VA_ARGS__

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
