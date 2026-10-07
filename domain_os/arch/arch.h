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
 *   - Module data blocks (docs/design-per-process-data.md, section 3):
 *       MODULE_DATA_DEFINE(T, name, addr)            - define the block
 *       MODULE_DATA_DEFINE_INIT(T, name, addr, init) - same, with the
 *                                                      image's contents
 *     and, shared by every architecture (defined below):
 *       MODULE_DATA_DECLARE(T, name, addr)           - declare it (headers)
 *       MODULE_DATA_ADDR(name)                       - its original
 *                                                      image address, a
 *                                                      uint32_t constant
 *                                                      expression (NOT the
 *                                                      link address)
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
 * labels.  It is the block's address in the IMAGE, not where the linker
 * puts it: since the owner's decision of 2026-09-28 the kernel is linked in
 * the SAU2 map's order, not at its addresses, and the address is the key
 * tools/gen_layout_ld.py orders the block by (`make check' proves the
 * order).  On every build the object's real address is `&name' /
 * ARCH_PTR_TO_VA(&name); MODULE_DATA_ADDR only documents the image.
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

/*
 * Pascal parameter slots - calling hand-assembled routines from C.
 *
 * Domain Pascal pushes a 16-bit value parameter as a 2-byte stack slot
 * (`move.w x,-(sp)') and an 8-bit one the same way (the byte in the low half
 * of the word, i.e. at the word's SECOND byte address).  gcc pushes every
 * scalar argument as a 4-byte slot, the value in the slot's low half (its
 * HIGHER-addressed word on the big-endian m68k).  The routines in
 * <sub>/sau2/<name>.s are the image's bytes and read their word parameters at the
 * Pascal offsets, i.e. at the slot's FIRST (lower-addressed) word.  Example,
 * ML_$SPIN_UNLOCK at 0xE20BBE, bytes 46 EF 00 08 4E 75:
 *     move.w (8,sp),sr      ; Pascal frame: 4 = lockp.l, 8 = token.w
 * Called by gcc as f(lockp, token) the token is at 10, and SR := the empty
 * word at 8 (= 0: user mode, IPL 0) - the first boot stop (source-nxtd).
 *
 * The C side builds the Pascal layout instead: a word parameter that the
 * Pascal caller pushed alone occupies one gcc slot whose FIRST word holds it:
 *     ARCH_PASCAL_WORD_SLOT(w)          = w << 16
 * Two ADJACENT word parameters f(..., a.w, b.w, ...) were pushed b then a
 * (right to left), so a is at the lower address: one gcc slot, a first:
 *     ARCH_PASCAL_WORD_PAIR_SLOT(a, b)  = a << 16 | b
 * A lone byte parameter is a word slot holding the byte in the word's low
 * half (`move.b (5,sp)' in MMU_$SET_CSR reads it), so it is packed with
 * ARCH_PASCAL_WORD_SLOT((uint8_t)v); likewise a byte in a pair.
 *
 * The packing is identical on every architecture (it is a value, not a
 * memory layout), so host tests and host stubs exercise it; C definitions
 * and stubs of such routines unpack with ARCH_PASCAL_SLOT_WORD (the first /
 * only word) and ARCH_PASCAL_SLOT_WORD2 (the second word of a pair).
 * Owners' public headers declare the routine with a uint32_t `<param>_slot'
 * and define a same-name function-like macro that packs the natural
 * arguments; definitions write the name parenthesised to defeat the macro:
 *     void (ML_$SPIN_UNLOCK)(void *lockp, uint32_t token_slot) { ... }
 * Words a routine RETURNS need nothing: D0.w is the low half gcc reads.
 */
#define ARCH_PASCAL_WORD_SLOT(w)         ((uint32_t)(uint16_t)(w) << 16)
#define ARCH_PASCAL_WORD_PAIR_SLOT(a, b) \
    (((uint32_t)(uint16_t)(a) << 16) | (uint32_t)(uint16_t)(b))
#define ARCH_PASCAL_SLOT_WORD(s)         ((uint16_t)((uint32_t)(s) >> 16))
#define ARCH_PASCAL_SLOT_WORD2(s)        ((uint16_t)(uint32_t)(s))

/*
 * A Pascal frame that puts an UNPADDED word before a longword (e.g.
 * IO_$TRAP: (4) vector.w, (6) handler.l) makes the longword straddle two
 * gcc slots: the caller packs PAIR(word, l >> 16) then a slot whose first
 * word is l's low half; an implementation reassembles the longword with
 * ARCH_PASCAL_SLOTS_LONG(first_slot, next_slot).
 */
#define ARCH_PASCAL_SLOTS_LONG(a, b) \
    (((uint32_t)ARCH_PASCAL_SLOT_WORD2(a) << 16) | ARCH_PASCAL_SLOT_WORD(b))

#endif /* ARCH_H */