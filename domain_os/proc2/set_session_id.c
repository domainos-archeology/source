/*
 * PROC2_$SET_SESSION_ID - Set session ID for current process
 *
 * Sets the session ID for the current process. Various validation checks
 * are performed to ensure the operation is valid:
 *
 * - If setting to same session ID and process is group leader, error
 * - If setting to different session and process has a pgroup, error
 * - If session ID is in use by another process's pgroup, error
 *
 * Parameters:
 *   flags      - Pointer to flags byte (high bit controls group leader check)
 *   session_id - Pointer to new session ID
 *   status_ret - Returns status (0 on success)
 *
 * Original address: 0x00e41c42
 */

#include "proc2/proc2_internal.h"

/*
 * Raw memory access macros for session-related fields
 */
#if defined(M68K)
    #define P2_SS_BASE(idx)            ((uint8_t*)(0xEA551C + ((idx) * 0xE4)))

    /* Offset 0x32 - UPID field */
    #define P2_SS_UPID(idx)            (*(int16_t*)(0xEA544E + (idx) * 0xE4))

    /* Offset 0x78 - session ID */
    #define P2_SS_SESSION(idx)         (*(int16_t*)(0xEA5494 + (idx) * 0xE4))

    /* Offset 0x10 - pgroup index */
    #define P2_SS_PGROUP(idx)          (*(int16_t*)(0xEA5448 + (idx) * 0xE4))

    /* Entry pointer for helpers */
    #define P2_SS_ENTRY(idx)           ((proc2_info_t*)(0xEA5438 + (idx) * 0xE4))
#else
    static int16_t p2_ss_dummy16;
    #define P2_SS_UPID(idx)            (p2_ss_dummy16)
    #define P2_SS_SESSION(idx)         (p2_ss_dummy16)
    #define P2_SS_PGROUP(idx)          (p2_ss_dummy16)
    #define P2_SS_ENTRY(idx)           ((proc2_info_t*)0)
#endif

void PROC2_$SET_SESSION_ID(int8_t *flags, int16_t *session_id, status_$t *status_ret)
{
    int16_t cur_idx;
    int16_t new_session;
    int16_t pgroup_idx;
    int8_t flag_byte;
    status_$t status;
    int can_set;

    flag_byte = *flags;
    new_session = *session_id;

    ML_$LOCK(PROC2_LOCK_ID);

    status = 0;
    can_set = 1;

    /* Get current process index */
    cur_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);

    /* Check if setting to same session ID */
    if (new_session == P2_SS_UPID(cur_idx)) {
        /* Same session - check if we're the group leader */
        if (new_session != 0) {
            pgroup_idx = PGROUP_FIND_BY_UPGID(new_session);

            if (P2_SS_PGROUP(cur_idx) != 0 &&
                P2_SS_PGROUP(cur_idx) == pgroup_idx) {
                /* We are the group leader */
                if (flag_byte >= 0) {
                    /* Flags high bit not set - error */
                    status = status_$proc2_process_is_group_leader;
                    can_set = 0;
                }
            } else if (pgroup_idx != 0) {
                /* Session ID is in use by another pgroup */
                status = status_$proc2_process_using_pgroup_id;
            }
        }
    } else {
        /* Different session - check if we have a pgroup */
        if (new_session != 0 &&
            P2_SS_SESSION(cur_idx) != 0 &&
            P2_SS_PGROUP(cur_idx) != 0) {
            status = status_$proc2_pgroup_in_different_session;
            can_set = 0;
        }
    }

    /* If status is still OK, we can set the session */
    if (status == 0) {
        can_set = 1;
    }

    if (can_set && status == 0) {
        /* Call helper to prepare for session change */
        PGROUP_CLEANUP_INTERNAL(P2_SS_ENTRY(cur_idx), 2);

        /* Set the new session ID */
        P2_SS_SESSION(cur_idx) = new_session;

        /* Call helper to complete session setup */
        PGROUP_SET_INTERNAL(P2_SS_ENTRY(cur_idx), new_session, &status);
    }

    ML_$UNLOCK(PROC2_LOCK_ID);

    *status_ret = status;
}
