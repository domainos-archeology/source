/*
 * PROC2_$AWAKEN_GUARDIAN - Awaken the guardian/debugger process
 *
 * Notifies the guardian (debugger or parent) process that something
 * has happened to a child process. Sends SIGTSTP and SIGCHLD signals
 * and advances eventcounts to wake up any waiting processes.
 *
 * Parameters:
 *   proc_index - Pointer to process table index
 *
 * Original address: 0x00e3e960
 */

#include "proc2.h"
#include "ec/ec.h"

/* External declarations */
extern void PROC2_$DELIVER_SIGNAL_INTERNAL(int16_t index, int16_t signal,
                                            uint32_t param, status_$t *status_ret);
extern ec_$eventcount_t AS_$CR_REC_FILE_SIZE;  /* Base of EC array */
extern ec_$eventcount_t AS_$INIT_STACK_FILE_SIZE;  /* Another EC array */

/*
 * Raw memory access macros for guardian-related fields
 */
#if defined(M68K)
    #define P2_AG_BASE(idx)            ((uint8_t*)(0xEA551C + ((idx) * 0xE4)))

    /* Offset 0x5E - debugger/XPD index */
    #define P2_DEBUGGER_IDX(idx)       (*(int16_t*)(0xEA545E + (idx) * 0xE4))

    /* Offset 0x56 - alternate guardian index */
    #define P2_ALT_GUARDIAN(idx)       (*(int16_t*)(0xEA5456 + (idx) * 0xE4))

    /* Offset 0x2B - flags byte 1 */
    #define P2_AG_FLAGS_B1(idx)        (*(uint8_t*)(0xEA5463 + (idx) * 0xE4))

    /* Offset 0x54 - self index for EC lookup */
    #define P2_AG_SELF_IDX(idx)        (*(int16_t*)(0xEA5454 + (idx) * 0xE4))

    /* EC array base */
    #define AG_CR_REC_EC(idx)          ((ec_$eventcount_t*)(0xE2B96C + (idx) * 24))
    #define AG_INIT_STACK_EC(idx)      ((ec_$eventcount_t*)(0xE2B960 + (idx) * 24))
#else
    static int16_t p2_ag_dummy16;
    static uint8_t p2_ag_dummy8;
    #define P2_DEBUGGER_IDX(idx)       (p2_ag_dummy16)
    #define P2_ALT_GUARDIAN(idx)       (p2_ag_dummy16)
    #define P2_AG_FLAGS_B1(idx)        (p2_ag_dummy8)
    #define P2_AG_SELF_IDX(idx)        (p2_ag_dummy16)
    #define AG_CR_REC_EC(idx)          ((ec_$eventcount_t*)0)
    #define AG_INIT_STACK_EC(idx)      ((ec_$eventcount_t*)0)
#endif

void PROC2_$AWAKEN_GUARDIAN(int16_t *proc_index)
{
    int16_t idx;
    int16_t guardian_idx;
    status_$t status;

    idx = *proc_index;

    /* Get debugger index, or fall back to alternate guardian */
    guardian_idx = P2_DEBUGGER_IDX(idx);
    if (guardian_idx == 0) {
        guardian_idx = P2_ALT_GUARDIAN(idx);
    }

    /* Clear flag bit 5 in flags byte 1 */
    P2_AG_FLAGS_B1(idx) &= 0xDF;

    if (guardian_idx != 0) {
        /* Send SIGTSTP (0x12 = 18) and SIGCHLD (0x17 = 23) to guardian */
        PROC2_$DELIVER_SIGNAL_INTERNAL(guardian_idx, SIGTSTP, 0, &status);
        PROC2_$DELIVER_SIGNAL_INTERNAL(guardian_idx, SIGCHLD, 0, &status);

        /* Advance the CR_REC eventcount for the guardian */
        EC_$ADVANCE(AG_CR_REC_EC(guardian_idx));
    }

    /* If we used the alternate guardian (not debugger), also advance init stack EC */
    if (guardian_idx != P2_DEBUGGER_IDX(idx)) {
        int16_t self_idx = P2_AG_SELF_IDX(idx);
        ec_$eventcount_t *ec = AG_INIT_STACK_EC(self_idx);

        EC_$ADVANCE(ec);

        /* If EC value is 0, advance again */
        if (*(int32_t*)ec == 0) {
            EC_$ADVANCE(ec);
        }
    }
}
