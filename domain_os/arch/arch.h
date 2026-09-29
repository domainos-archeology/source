/*
 * arch/arch.h - Architecture Selector
 *
 * Includes the correct architecture-specific header based on the
 * build-time ARCH_* define. Each architecture header must provide:
 *
 *   - Interrupt control macros:
 *       DISABLE_INTERRUPTS(sr)  - Save SR and disable all interrupts
 *       ENABLE_INTERRUPTS(sr)   - Restore saved SR
 *       GET_SR(sr_var)          - Read current status register
 *       SET_SR(sr_val)          - Write status register
 *
 *   - SR constants:
 *       SR_IPL_MASK, SR_IPL_DISABLE_ALL, SR_SUPERVISOR, SR_TRACE
 *
 *   - Memory model constants:
 *       ARCH_BIG_ENDIAN (if applicable), ARCH_PTR_SIZE,
 *       ARCH_ALIGN_16, ARCH_ALIGN_32
 *
 *   - Busy-wait primitive:
 *       ARCH_SPIN_TICK()        - one non-elidable delay-loop iteration
 *
 *   - Exception vector table entries:
 *       ARCH_VECTOR(n)          - lvalue for vector n's handler address
 *       ARCH_AUTOVECTOR(level)  - lvalue for interrupt level 1..7's vector
 *
 *   - Global data pointer:
 *       __A5_BASE()  (or a stub for non-M68K)
 *
 *   - Module data blocks (docs/design-per-process-data.md, section 3):
 *       MODULE_DATA_DEFINE(T, name, addr)            - define the block
 *       MODULE_DATA_DEFINE_INIT(T, name, addr, init) - same, with the
 *                                                      image's contents
 *     and, shared by every architecture (defined below):
 *       MODULE_DATA_DECLARE(T, name, addr)           - declare it (headers)
 *       MODULE_DATA_ADDR(name)                       - its original
 *                                                      address, a uint32_t
 *                                                      constant expression
 *
 * To add a new architecture, create arch/<arch>/arch.h and add
 * a new #elif block here.
 */

#ifndef ARCH_H
#define ARCH_H

#if defined(ARCH_M68K)
#include "arch/m68k/arch.h"
#elif defined(ARCH_HOST)
#include "arch/host/arch.h"
#else
#error "undefined architecture: define ARCH_M68K or ARCH_HOST"
#endif

/*
 * MODULE_DATA_DECLARE / MODULE_DATA_ADDR - the architecture-independent half
 * of the module data block family (the DEFINE half is in each arch header).
 *
 * The declaration, in the subsystem's header, carries the block's original
 * address as an enumeration constant, so MODULE_DATA_ADDR(name) is an
 * integer constant expression on every build: usable in _Static_assert
 * (block end against the link map's segment size), in static initialisers
 * of cells that hold another block's VA as image contents, and in case
 * labels.  On the target the object really is at that address (the linker
 * fragment tools/gen_moddata_ld.py writes places it there and `make
 * check-moddata' proves it); on the host it is wherever the compiler put it
 * and only the constant is the original address.
 *
 * The definition repeats the literal (the generator reads it from the
 * defining .c) and _Static_asserts it against the declaration, so the two
 * cannot drift; a definition therefore needs its declaration in scope, which
 * every _data.c has through its subsystem's internal header.
 *
 * The SAU2 address space is 24 bits, so every address fits an int, the
 * only type C guarantees for an enumeration constant.
 */
#define MODULE_DATA_DECLARE(T, name, addr)                                   \
    _Static_assert((addr) > 0 && (addr) <= 0x7FFFFFFF,                       \
                   #name ": module data address must fit an int");           \
    enum { moddata_addr_##name = (addr) };                                   \
    extern T name

#define MODULE_DATA_ADDR(name) ((uint32_t)moddata_addr_##name)

#endif /* ARCH_H */