/*
 * ML_$SPIN_LOCK - Acquire a spin lock (SAU2 implementation)
 *
 * On SAU2 (single-processor Apollo systems), spin locks are implemented
 * by simply disabling interrupts. The lock parameter is not actually used
 * since there's no contention possible with interrupts disabled.
 *
 * Parameters:
 *   4(SP) - void *lockp (unused on SAU2)
 *
 * Returns:
 *   D0.W - Previous status register value (token for unlock)
 *
 * Original address: 0x00E20BB6
 */

        .text
        .globl  ML_$SPIN_LOCK

ML_$SPIN_LOCK:
        move.w  %sr,%d0                 /* Save current SR */
        ori.w   #0x0700,%sr             /* Disable interrupts (IPL = 7) */
        rts
