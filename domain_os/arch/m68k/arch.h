/*
 * arch/m68k/arch.h - M68K Architecture Definitions
 *
 * Main include file for M68K-specific definitions.
 * This aggregates all architecture-specific headers for the M68K platform.
 *
 * When porting to a new architecture, create arch/<arch>/arch.h
 * with equivalent functionality.
 */

#ifndef ARCH_M68K_ARCH_H
#define ARCH_M68K_ARCH_H

/* Interrupt control (DISABLE_INTERRUPTS, ENABLE_INTERRUPTS, etc.) */
#include "arch/m68k/intr.h"

/*
 * M68K memory model:
 *   - Big-endian byte order
 *   - 32-bit pointers
 *   - Natural alignment: 2-byte for 16-bit, 4-byte for 32-bit
 */
#define ARCH_BIG_ENDIAN    1
#define ARCH_PTR_SIZE      4
#define ARCH_ALIGN_16      2
#define ARCH_ALIGN_32      4

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
 * Several kernel records carry m68k 32-bit virtual addresses in uint32_t
 * fields rather than in pointers, because that is what the binary stores
 * (mac_os_$buf_desc_t.address, mac_os_$send_pkt_t.data_pages,
 * rip_$send_frame_t.hdr_va, ...).  On the target a virtual address IS a
 * pointer, so both macros are plain casts and generate no code.
 */
#define ARCH_VA_TO_PTR(va) ((void *)(uintptr_t)(va))
#define ARCH_PTR_TO_VA(p)  ((uint32_t)(uintptr_t)(p))

/*
 * M68K Global Register Variables
 *
 * The A5 register is used as the global data pointer in Domain/OS.
 * Many kernel data structures are accessed via fixed offsets from A5.
 *
 * __A5_BASE() returns the value of the A5 register as a void pointer.
 * Use this macro to access A5-relative globals:
 *   *(uint32_t *)((char *)__A5_BASE() + offset)
 */
static inline void *__A5_BASE(void) {
    void *result;
    __asm__ ("move.l %%a5, %0" : "=r" (result));
    return result;
}

#endif /* ARCH_M68K_ARCH_H */
