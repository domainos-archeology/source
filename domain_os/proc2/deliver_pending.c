/*
 * PROC2_$DELIVER_PENDING - Deliver pending signals to current process
 *
 * Public wrapper that clears the FIM quit inhibit flag and calls
 * DELIVER_PENDING_INTERNAL for the current process.
 *
 * Parameters:
 *   status_ret - Pointer to receive status (for API compatibility)
 *
 * Note: The decompiler shows this function takes no parameters,
 * but the header declares it with a status_ret parameter.
 * We implement it matching the header for API compatibility.
 *
 * Original address: 0x00e3f520
 */

#include "proc2.h"

/* FIM quit inhibit table */
#if defined(M68K)
    #define FIM_QUIT_INH_TABLE      ((uint8_t*)0xE2248A)
#else
    extern uint8_t *fim_quit_inh_table;
    #define FIM_QUIT_INH_TABLE      fim_quit_inh_table
#endif

void PROC2_$DELIVER_PENDING(status_$t *status_ret)
{
    int16_t current_idx;

    /* Get current process index */
    current_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);

    ML_$LOCK(PROC2_LOCK_ID);

    /* Clear FIM quit inhibit for current ASID */
    FIM_QUIT_INH_TABLE[PROC1_$AS_ID] = 0;

    /* Deliver any pending signals */
    PROC2_$DELIVER_PENDING_INTERNAL(current_idx);

    ML_$UNLOCK(PROC2_LOCK_ID);

    if (status_ret != NULL) {
        *status_ret = status_$ok;
    }
}
