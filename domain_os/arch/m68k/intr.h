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
 * Status Register (SR) bit definitions
 */
#define SR_IPL_MASK             0x0700  /* Interrupt priority level bits */
#define SR_IPL_DISABLE_ALL      0x0700  /* Disable all interrupts (IPL=7) */
#define SR_SUPERVISOR           0x2000  /* Supervisor mode bit */
#define SR_TRACE                0x8000  /* Trace mode bit */

/*
 * DISABLE_INTERRUPTS - Save SR and raise interrupt priority to 7
 *
 * This macro must be paired with ENABLE_INTERRUPTS in the same scope.
 * It declares a local variable to save the previous SR state.
 *
 * Usage:
 *   uint16_t sr;
 *   DISABLE_INTERRUPTS(sr);
 *   // critical section
 *   ENABLE_INTERRUPTS(sr);
 */
#define DISABLE_INTERRUPTS(sr) \
    __asm__ volatile ( \
        "move.w %%sr, %0\n\t" \
        "ori.w #0x0700, %%sr" \
        : "=d" (sr) \
        : \
        : "cc", "memory" \
    )

/*
 * ENABLE_INTERRUPTS - Restore SR to previously saved state
 *
 * Must be called after DISABLE_INTERRUPTS in the same scope.
 */
#define ENABLE_INTERRUPTS(sr) \
    __asm__ volatile ( \
        "move.w %0, %%sr" \
        : \
        : "d" (sr) \
        : "cc", "memory" \
    )

/*
 * SET_IPL7 - Raise the interrupt priority to 7 WITHOUT saving the old SR
 *
 * Models a bare `ori #0x700,SR` in the original code.  Unlike
 * DISABLE_INTERRUPTS there is no saved value, because the corresponding
 * exit is a forced `andi #-0x701,SR` (SET_IPL0) rather than a restore.
 * See ML_$EXCLUSION_STOP (0x00E20E8A, 0x00E20EAC) and ML_$UNLOCK
 * (0x00E20B6A).
 */
#define SET_IPL7() \
    __asm__ volatile ("ori.w #0x0700, %%sr" : : : "cc", "memory")

/*
 * SET_IPL0 - Force the interrupt priority level to 0
 *
 * Models `andi #-0x701,SR` (i.e. andi.w #0xF8FF,SR), which clears the IPL
 * field outright.  This is NOT a restore of a previously saved SR: the
 * Domain kernel's lock-release paths deliberately drop to IPL 0 on exit.
 * See ML_$UNLOCK 0x00E20EEA and ML_$EXCLUSION_STOP 0x00E20E9C.
 */
#define SET_IPL0() \
    __asm__ volatile ("andi.w #0xF8FF, %%sr" : : : "cc", "memory")

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
    __asm__ volatile ("move.w %0, %%sr" : : "d" (sr_val) : "cc", "memory")

#endif /* ARCH_M68K_INTR_H */
