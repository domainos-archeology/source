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

/* Hardware register and device memory addresses of the SAU being built
 * (only the SAU2 today). */
#if defined(SAU2)
#include "arch/m68k/sau2/hw.h"
#else
#error "arch/m68k: no hardware address header for this SAU"
#endif

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
 * ARCH_PTR_TO_VA_STATIC(p, image_va) - ARCH_PTR_TO_VA for a static
 * initialiser: a uint32_t cell that holds the VA of a linked object as image
 * contents (STOP_$DATA.wire_start = STOP_$WATCH).  On the target it is the
 * link-time address of `p' - the kernel is no longer linked at the image's
 * addresses, only in its order (tools/gen_layout_ld.py) - and `image_va',
 * the value the image carries, is documentation.
 */
#define ARCH_PTR_TO_VA_STATIC(p, image_va) ARCH_PTR_TO_VA(p)

/*
 * ARCH_VECTOR / ARCH_AUTOVECTOR - CPU exception vector table entries
 *
 * The SAU2 68020 runs with VBR = 0, so the exception vector table is the
 * first 1KB of physical memory and vector n is the longword at n * 4.
 * ARCH_AUTOVECTOR(level) names the level-1..7 autovector entries, which are
 * vectors 25..31 (0x64..0x7C); SMD_$INTERRUPT_INIT's "move.l A0,(0x70).l"
 * at 0x00E272A0 is ARCH_AUTOVECTOR(4).
 *
 * Both expand to an lvalue holding a routine address, so an installer writes
 * ARCH_AUTOVECTOR(n) = &handler;
 */
#define ARCH_VECTOR(n)         (*(void *volatile *)((uintptr_t)(n) * 4u))
#define ARCH_AUTOVECTOR(level) ARCH_VECTOR(24u + (level))

/*
 * MODULE_DATA_* - Pascal module data blocks, linked in the image's order
 *
 * Domain Pascal compiles each module's globals into one block addressed
 * (off,A5).  A block is modelled as a single C object of a struct type whose
 * fields sit at the A5 displacements (docs/design-per-process-data.md,
 * section 3).  MODULE_DATA_DEFINE puts it in its own input section
 * `.moddata.<name>', and the link-order fragment that tools/gen_layout_ld.py
 * writes (build/sau2/layout.ld, INCLUDEd by sau2.ld) lists that section
 * among the code at the position its original address has in the SAU2 link
 * map - after its module's code when the image has it there.  `make check'
 * links a scratch ELF and proves the order.
 *
 * The address given to these macros is the block's ORIGINAL address in the
 * image: documentation and the generator's ordering key.  It is NOT the
 * link address (the owner's decision of 2026-09-28: the relative placement
 * must match the image, the absolute addresses need not).  Anything that
 * needs where the block really is takes `&name' / ARCH_PTR_TO_VA(&name).
 *
 *   MODULE_DATA_DEFINE(T, name, addr)          zero-filled block
 *   MODULE_DATA_DEFINE_INIT(T, name, addr, {...})
 *                                              block with the image's
 *                                              initial contents
 *   MODULE_DATA_DECLARE(T, name, addr)         the extern, for a header
 *   MODULE_DATA_ADDR(name)                     the block's original image
 *                                              address as a 32-bit constant
 *                                              (both in arch/arch.h)
 *
 * `addr' must be a literal (the generator reads it from the source text),
 * even (68000-family word alignment; the generator and the _Static_assert
 * below both refuse an odd one) and equal to the address in the block's
 * MODULE_DATA_DECLARE, which must be in scope.  The section is progbits
 * even for a zero-filled block, so the RFC image carries the block like any
 * other loaded byte.  `used' keeps an unreferenced block alive so its
 * position is still checked.
 */
#define MODULE_DATA_ATTRS_(name) \
    __attribute__((section(".moddata." #name), aligned(2), used))

#define MODULE_DATA_CHECK_ADDR_(name, addr)                                  \
    _Static_assert(((addr) & 1u) == 0u,                                      \
                   #name ": module data address must be even");              \
    _Static_assert((addr) == moddata_addr_##name,                            \
                   #name ": address differs from its MODULE_DATA_DECLARE")

#define MODULE_DATA_DEFINE(T, name, addr)                                    \
    MODULE_DATA_CHECK_ADDR_(name, addr);                                     \
    T name MODULE_DATA_ATTRS_(name)

#define MODULE_DATA_DEFINE_INIT(T, name, addr, ...)                          \
    MODULE_DATA_CHECK_ADDR_(name, addr);                                     \
    T name MODULE_DATA_ATTRS_(name) = __VA_ARGS__

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
