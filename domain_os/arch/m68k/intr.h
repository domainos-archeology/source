/*
 * arch/m68k/intr.h - M68K Interrupt Control
 *
 * Architecture-specific macros for interrupt manipulation on M68K processors.
 * These manipulate the Status Register (SR) to control interrupt priority.
 *
 * M68K SR layout (bits 8-10 = interrupt priority mask):
 *   Bits 15-13: Trace mode
 *   Bit 13:     Supervisor mode
 *   Bits 10-8:  Interrupt priority mask (0-7, 7 = all masked)
 *   Bits 4-0:   Condition codes (XNZVC)
 *
 * Setting IPL to 7 (0x0700) blocks all maskable interrupts.
 */

#ifndef ARCH_M68K_INTR_H
#define ARCH_M68K_INTR_H

/*
 * DISABLE_INTERRUPTS - Save SR and raise interrupt priority to 7
 *
 * This macro must be paired with ENABLE_INTERRUPTS in the same scope.
 * It declares a local variable to save the previous SR state.
 *
 * Usage:
 *   DISABLE_INTERRUPTS();
 *   // critical section
 *   ENABLE_INTERRUPTS();
 */
#define DISABLE_INTERRUPTS() \
    uint16_t _saved_sr; \
    __asm__ volatile ( \
        "move.w %%sr, %0\n\t" \
        "ori.w #0x0700, %%sr" \
        : "=d" (_saved_sr) \
        : \
        : "cc" \
    )

/*
 * ENABLE_INTERRUPTS - Restore SR to previously saved state
 *
 * Must be called after DISABLE_INTERRUPTS in the same scope.
 */
#define ENABLE_INTERRUPTS() \
    __asm__ volatile ( \
        "move.w %0, %%sr" \
        : \
        : "d" (_saved_sr) \
        : "cc" \
    )

/*
 * GET_SR - Read the current status register
 *
 * Returns the current SR value without modifying it.
 */
#define GET_SR(sr_var) \
    __asm__ volatile ("move.w %%sr, %0" : "=d" (sr_var))

/*
 * SET_SR - Write a new value to the status register
 *
 * Note: This is a privileged operation (supervisor mode only).
 */
#define SET_SR(sr_val) \
    __asm__ volatile ("move.w %0, %%sr" : : "d" (sr_val) : "cc")

#endif /* ARCH_M68K_INTR_H */
