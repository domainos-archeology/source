/*
 * ML_$SPIN_UNLOCK - Release a spin lock (SAU2 implementation)
 *
 * On SAU2 (single-processor Apollo systems), spin locks are implemented
 * by simply disabling/restoring interrupts. This function restores the
 * status register to the value saved by ML_$SPIN_LOCK.
 *
 * Parameters:
 *   4(SP) - void *lockp (unused on SAU2)
 *   8(SP) - uint16_t token (saved SR from ML_$SPIN_LOCK)
 *
 * Original address: 0x00E20BBE
 */

        .text
        .globl  ML_$SPIN_UNLOCK

ML_$SPIN_UNLOCK:
        move.w  8(%sp),%sr              /* Restore SR from token */
        rts
