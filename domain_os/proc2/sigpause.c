/*
 * PROC2_$SIGPAUSE - Pause waiting for signal
 *
 * Temporarily replaces the signal mask and waits for a signal to be
 * delivered. The process blocks on FIM_$QUIT_EC until a signal arrives.
 *
 * Parameters:
 *   new_mask - Pointer to new signal mask value
 *   result   - Returns old mask value and flag
 *
 * Original address: 0x00e3fa10
 */

#include "proc2/proc2_internal.h"

/*
 * Raw memory access macros for SIGPAUSE fields
 */
#if defined(ARCH_M68K)
    #define P2_SP_MASK2(idx)           (*(uint32_t*)(0xEA54B0 + (idx) * 0xE4))
    #define P2_SP_ALT_MASK(idx)        (*(uint32_t*)(0xEA54C0 + (idx) * 0xE4))
    #define P2_SP_BLOCKED2(idx)        (*(uint32_t*)(0xEA54B8 + (idx) * 0xE4))
    #define P2_SP_FLAGS_B(idx)         (*(uint8_t*)(0xEA5462 + (idx) * 0xE4))
    #define P2_SP_FLAGS_W(idx)         (*(uint16_t*)(0xEA5462 + (idx) * 0xE4))
    #define P2_SP_SELF_IDX(idx)        (*(int16_t*)(0xEA5454 + (idx) * 0xE4))

    /* FIM arrays */
    #define FIM_QUIT_EC_ENTRY(asid)    ((ec_$eventcount_t*)(0xE22002 + (asid) * 12))
    #define FIM_QUIT_VALUE_ENTRY(asid) (*(uint32_t*)(0xE222BA + (asid) * 4))
#else
    static uint32_t p2_sp_dummy32;
    static uint8_t p2_sp_dummy8;
    static uint16_t p2_sp_dummy_u16;
    static int16_t p2_sp_dummy16;
    #define P2_SP_MASK2(idx)           (p2_sp_dummy32)
    #define P2_SP_ALT_MASK(idx)        (p2_sp_dummy32)
    #define P2_SP_BLOCKED2(idx)        (p2_sp_dummy32)
    #define P2_SP_FLAGS_B(idx)         (p2_sp_dummy8)
    #define P2_SP_FLAGS_W(idx)         (p2_sp_dummy_u16)
    #define P2_SP_SELF_IDX(idx)        (p2_sp_dummy16)
    #define FIM_QUIT_EC_ENTRY(asid)    ((ec_$eventcount_t*)0)
    #define FIM_QUIT_VALUE_ENTRY(asid) (p2_sp_dummy32)
#endif

void PROC2_$SIGPAUSE(uint32_t *new_mask, uint32_t *result)
{
    int16_t cur_idx;
    uint32_t mask_val;
    int32_t wait_val;
    ec_$eventcount_t *ec_array[1];
    int32_t val_array[1];

    mask_val = *new_mask;

    /* Get current process index */
    cur_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);

    ML_$LOCK(PROC2_LOCK_ID);

    /* Save current mask2 to alt_mask and set new mask */
    P2_SP_ALT_MASK(cur_idx) = P2_SP_MASK2(cur_idx);
    P2_SP_MASK2(cur_idx) = mask_val;

    /* Set flag bit 6 (0x40) */
    P2_SP_FLAGS_B(cur_idx) |= 0x40;

    ML_$UNLOCK(PROC2_LOCK_ID);

    /* Return old mask and flag */
    result[0] = P2_SP_MASK2(cur_idx);

    if ((P2_SP_FLAGS_W(cur_idx) & 0x0400) != 0) {
        result[1] = 1;
    } else {
        result[1] = 0;
    }

    /* Set up eventcount to wait on */
    ec_array[0] = FIM_QUIT_EC_ENTRY(PROC1_$AS_ID);

    /* Wait loop - check for unblocked pending signals */
    while (1) {
        wait_val = FIM_QUIT_VALUE_ENTRY(PROC1_$AS_ID) + 1;
        val_array[0] = wait_val;

        /* Check if any blocked signals are now unblocked */
        if ((P2_SP_BLOCKED2(cur_idx) & ~P2_SP_MASK2(cur_idx)) != 0) {
            /* Signal arrived - exit wait loop */
            break;
        }

        /* Wait on the quit eventcount */
        EC_$WAITN(ec_array, val_array, 1);

        /* Update quit value from eventcount */
#if defined(ARCH_M68K)
        FIM_QUIT_VALUE_ENTRY(PROC1_$AS_ID) = *(int32_t*)FIM_QUIT_EC_ENTRY(PROC1_$AS_ID);
#endif
    }

    /* Signal arrived - deliver pending signals */
    ML_$LOCK(PROC2_LOCK_ID);
    PROC2_$DELIVER_PENDING_INTERNAL(P2_SP_SELF_IDX(cur_idx));
    ML_$UNLOCK(PROC2_LOCK_ID);
}
