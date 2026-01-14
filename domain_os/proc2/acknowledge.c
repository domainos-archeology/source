/*
 * PROC2_$ACKNOWLEDGE - Acknowledge signal delivery
 *
 * Called by signal handlers to acknowledge receipt and completion of
 * signal handling. Performs the following:
 *
 * 1. Updates signal masks (clears pending/blocked bits)
 * 2. For job control signals (SIGTSTP, SIGSTOP, etc.), may suspend process
 * 3. For SIGCONT, may notify parent or wake up debugger
 * 4. Delivers any remaining pending signals
 *
 * Parameters:
 *   handler_addr - Pointer to signal handler address (stored in proc2_info)
 *   signal       - Pointer to signal number being acknowledged
 *   result       - Returns handler address and a flag word
 *
 * Result structure:
 *   result[0] = handler address
 *   result[1] = flag (1 if flag bit 10 set in process flags, 0 otherwise)
 *
 * Original address: 0x00e3f338
 */

#include "proc2.h"
#include "ec/ec.h"

/* External declarations */
extern void PROC2_$DELIVER_SIGNAL_INTERNAL(int16_t index, int16_t signal,
                                            uint32_t param, status_$t *status_ret);
extern void PROC2_$DELIVER_PENDING_INTERNAL(int16_t index);
extern void FUN_00e0a96c(void);  /* Unknown function - called during acknowledge */
extern ec_$eventcount_t AS_$CR_REC_FILE_SIZE;  /* Base for eventcount array */

/*
 * Signal mask constants
 * These signals have special job control behavior:
 *   SIGSTOP (17), SIGTSTP (18), SIGTTIN (21), SIGTTOU (22)
 * = bits 16, 17, 20, 21 = 0x00300003 inverted = 0xFFCFFFC
 * The code uses ~0x1980001 = 0xFE67FFFE to check for job control signals
 */
#define SIGNAL_JOB_CONTROL_MASK 0xFE67FFFF

/*
 * Raw memory access macros for signal-related fields
 * These are within proc2_info_t but not all are documented yet
 */
#if defined(M68K)
    #define P2_ACK_BASE(idx)           ((uint8_t*)(0xEA551C + ((idx) * 0xE4)))

    /* Offset 0x78 (0x44 from table base 0xEA54B0 area) - handler storage */
    #define P2_HANDLER_STORE(idx)      (*(uint32_t*)(0xEA54B0 + (idx) * 0xE4))

    /* Offset 0x58 - signal blocked mask 2 */
    #define P2_SIG_BLOCKED2(idx)       (*(uint32_t*)(0xEA54B8 + (idx) * 0xE4))

    /* Offset 0x54 - signal blocked mask 1 */
    #define P2_SIG_BLOCKED1(idx)       (*(uint32_t*)(0xEA54B4 + (idx) * 0xE4))

    /* Offset 0x2A - flags */
    #define P2_ACK_FLAGS(idx)          (*(uint16_t*)(0xEA5462 + (idx) * 0xE4))
    #define P2_ACK_FLAGS_B(idx)        (*(uint8_t*)(0xEA5462 + (idx) * 0xE4))
    #define P2_ACK_FLAGS_B2(idx)       (*(uint8_t*)(0xEA5463 + (idx) * 0xE4))

    /* Offset 0x4C area - pending signals */
    #define P2_SIG_PENDING(idx)        (*(uint32_t*)(0xEA54A8 + (idx) * 0xE4))

    /* Offset 0x10 - parent index */
    #define P2_ACK_PARENT(idx)         (*(int16_t*)(0xEA5448 + (idx) * 0xE4))

    /* Offset 0x1C - some index */
    #define P2_ACK_IDX1(idx)           (*(int16_t*)(0xEA5454 + (idx) * 0xE4))

    /* Offset 0x7C - stored signal number */
    #define P2_STORED_SIG(idx)         (*(int16_t*)(0xEA54CC + (idx) * 0xE4))

    /* Offset 0x1E - debugger/pgroup index */
    #define P2_ACK_IDX2(idx)           (*(int16_t*)(0xEA5456 + (idx) * 0xE4))

    /* Offset 0x60 - another signal mask */
    #define P2_SIG_MASK3(idx)          (*(uint32_t*)(0xEA54BC + (idx) * 0xE4))

    /* Offset 0x9A - level1_pid */
    #define P2_ACK_L1PID(idx)          (*(uint16_t*)(0xEA54D2 + (idx) * 0xE4))

    /* Parent UPID lookup (8-byte entries) */
    #define P2_PARENT_UPID_VAL(idx)    (*(int16_t*)(0xEA944E + (idx) * 8))
#else
    static uint32_t p2_ack_dummy32;
    static uint16_t p2_ack_dummy16;
    static int16_t p2_ack_dummy_s16;
    static uint8_t p2_ack_dummy8;
    #define P2_HANDLER_STORE(idx)      (p2_ack_dummy32)
    #define P2_SIG_BLOCKED2(idx)       (p2_ack_dummy32)
    #define P2_SIG_BLOCKED1(idx)       (p2_ack_dummy32)
    #define P2_ACK_FLAGS(idx)          (p2_ack_dummy16)
    #define P2_ACK_FLAGS_B(idx)        (p2_ack_dummy8)
    #define P2_ACK_FLAGS_B2(idx)       (p2_ack_dummy8)
    #define P2_SIG_PENDING(idx)        (p2_ack_dummy32)
    #define P2_ACK_PARENT(idx)         (p2_ack_dummy_s16)
    #define P2_ACK_IDX1(idx)           (p2_ack_dummy_s16)
    #define P2_STORED_SIG(idx)         (p2_ack_dummy_s16)
    #define P2_ACK_IDX2(idx)           (p2_ack_dummy_s16)
    #define P2_SIG_MASK3(idx)          (p2_ack_dummy32)
    #define P2_ACK_L1PID(idx)          (p2_ack_dummy16)
    #define P2_PARENT_UPID_VAL(idx)    (p2_ack_dummy_s16)
#endif

void PROC2_$ACKNOWLEDGE(uint32_t *handler_addr, int16_t *signal, uint32_t *result)
{
    int16_t cur_idx;
    uint32_t sig_mask;
    int8_t was_blocked;
    status_$t status;

    /* Compute signal bit mask: bit (signal-1) */
    sig_mask = 1U << (((uint16_t)(*signal - 1)) & 0x1F);

    /* Get current process index */
    cur_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);

    ML_$LOCK(PROC2_LOCK_ID);

    /* Store handler address in proc2_info */
    P2_HANDLER_STORE(cur_idx) = *handler_addr;

    /* Check if signal was in blocked mask 2 */
    if ((sig_mask & ~P2_SIG_BLOCKED2(cur_idx)) == 0) {
        /* Signal was blocked - clear it from blocked mask */
        P2_SIG_BLOCKED2(cur_idx) &= ~sig_mask;
        was_blocked = -1;
    } else {
        was_blocked = 0;
    }

    /* Check if signal was in blocked mask 1 */
    if ((sig_mask & ~P2_SIG_BLOCKED1(cur_idx)) == 0) {
        /* Set flag bit 2 in flags */
        P2_ACK_FLAGS_B(cur_idx) |= 0x04;
    }

    /* Call unknown function (possibly signal mask update) */
    FUN_00e0a96c();

    /* Check for job control signals that require special handling */
    /* Mask 0xFE67FFFF excludes SIGSTOP, SIGTSTP, SIGCONT, SIGTTIN, SIGTTOU */
    if ((sig_mask & SIGNAL_JOB_CONTROL_MASK) == 0) {
        /* This is a job control signal */
        if ((sig_mask & ~P2_SIG_PENDING(cur_idx)) != 0) {
            /* Signal not pending - handle it */

            if (was_blocked < 0) {
                /* Signal was previously blocked */
                int16_t parent_idx = P2_ACK_PARENT(cur_idx);

                /* Check conditions for sending SIGKILL to child vs suspending */
                int send_kill = 0;
                if (P2_PARENT_UPID_VAL(parent_idx) == 0 ||
                    P2_ACK_FLAGS(cur_idx) < 0 ||    /* Check high bit */
                    P2_ACK_PARENT(cur_idx) == 0) {
                    send_kill = 1;
                }

                /* But not if this is SIGCONT (bit 19 = 0x80000) */
                if ((sig_mask & 0x80000) != 0) {
                    send_kill = 0;
                }

                if (send_kill) {
                    /* Send SIGKILL (9) with param 0x9010009 to child process */
                    PROC2_$DELIVER_SIGNAL_INTERNAL(P2_ACK_IDX1(cur_idx), SIGKILL,
                                                    0x9010009, &status);
                } else {
                    /* Job control - suspend this process */
                    P2_STORED_SIG(cur_idx) = *signal;

                    /* Clear flags bits 4 and 5 (0xFFCF) */
                    P2_ACK_FLAGS(cur_idx) &= 0xFFCF;

                    /* Set flag bit 6 */
                    P2_ACK_FLAGS_B2(cur_idx) |= 0x40;

                    /* If flags bit 15 not set, notify parent/debugger */
                    if (P2_ACK_FLAGS(cur_idx) >= 0) {
                        /* Advance eventcount for debugger */
                        int16_t dbg_idx = P2_ACK_IDX2(cur_idx);
                        /* EC_$ADVANCE((ec_$eventcount_t*)
                                    (&AS_$CR_REC_FILE_SIZE + dbg_idx * 0x18)); */

                        /* If debugger doesn't have flag bit 2, send SIGCHLD (23) */
#if defined(M68K)
                        if ((*(uint8_t*)(0xEA5463 + dbg_idx * 0xE4) & 0x04) == 0) {
#else
                        if (0) { /* TODO: Non-M68K implementation */
#endif
                            PROC2_$DELIVER_SIGNAL_INTERNAL(P2_ACK_IDX2(cur_idx),
                                                           0x17 /* SIGCHLD */,
                                                           0x9010017, &status);
                        }
                    }

                    /* Suspend this process */
                    PROC1_$SUSPEND(P2_ACK_L1PID(cur_idx), &status);

                    /* Unlock and relock around suspension */
                    ML_$UNLOCK(PROC2_LOCK_ID);
                    ML_$LOCK(PROC2_LOCK_ID);
                }
            } else {
                /* Signal was not blocked - clear bit 5 of flags byte */
                P2_ACK_FLAGS_B2(cur_idx) &= 0xDF;
            }
        }
    }

    /* Check if signal should be cleared from pending mask */
    if ((sig_mask & ~P2_SIG_MASK3(cur_idx)) == 0) {
        P2_SIG_PENDING(cur_idx) &= ~sig_mask;
    }

    /* Deliver any remaining pending signals */
    PROC2_$DELIVER_PENDING_INTERNAL(P2_ACK_IDX1(cur_idx));

    ML_$UNLOCK(PROC2_LOCK_ID);

    /* Return results */
    result[0] = P2_HANDLER_STORE(cur_idx);

    /* Check flag bit 10 (0x400) */
    if ((P2_ACK_FLAGS(cur_idx) & 0x0400) != 0) {
        result[1] = 1;
    } else {
        result[1] = 0;
    }
}
