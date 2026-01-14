/*
 * PROC2_$SIGNAL - Send signal to process
 *
 * Sends a signal to a process after checking permissions.
 * Permission checks:
 *   - Same parent: allowed
 *   - Same session with SIGCONT: allowed
 *   - ACL check via ACL_$CHECK_FAULT_RIGHTS
 *
 * Parameters:
 *   proc_uid   - Pointer to target process UID
 *   signal     - Pointer to signal number
 *   param      - Pointer to signal parameter
 *   status_ret - Returns status (0 on success)
 *
 * Original address: 0x00e3efa0
 */

#include "proc2.h"

/* Forward declarations */
extern void PROC2_$DELIVER_SIGNAL_INTERNAL(int16_t index, int16_t signal,
                                           uint32_t param, status_$t *status_ret);
extern int8_t ACL_$CHECK_FAULT_RIGHTS(int16_t src_offset, int16_t dst_offset);

/* Log signal event (debugging) - currently a no-op */
static void log_signal_event(int event_type, int16_t target_idx, int16_t signal,
                            uint32_t param, status_$t status)
{
    (void)event_type;
    (void)target_idx;
    (void)signal;
    (void)param;
    (void)status;
}

/*
 * Raw memory access for undocumented parent/session fields
 */
#if defined(M68K)
    #define P2_SIGNAL_BASE(idx)      ((int16_t*)(0xEA551C + ((idx) * 0xE4)))
    /* offset -0xBE from entry end = field at 0x22 or so - likely parent-related */
    #define P2_PARENT_FIELD(idx)     (*(P2_SIGNAL_BASE(idx) - 0x5F))
    /* offset -0xC8 from entry end = field related to child idx */
    #define P2_CHILD_IDX_FIELD(idx)  (*(P2_SIGNAL_BASE(idx) - 0x64))
    /* offset -0x88 from entry end = session ID */
    #define P2_SESSION_ID(idx)       (*(P2_SIGNAL_BASE(idx) - 0x44))
#else
    static int16_t p2_signal_dummy;
    #define P2_PARENT_FIELD(idx)     (p2_signal_dummy)
    #define P2_CHILD_IDX_FIELD(idx)  (p2_signal_dummy)
    #define P2_SESSION_ID(idx)       (p2_signal_dummy)
#endif

void PROC2_$SIGNAL(uid_$t *proc_uid, int16_t *signal, uint32_t *param,
                   status_$t *status_ret)
{
    int16_t index;
    int16_t cur_index;
    proc2_info_t *target_info;
    proc2_info_t *cur_info;
    status_$t status;
    uint32_t param_copy;
    uid_$t uid_copy;
    int8_t acl_result;

    /* Copy inputs before locking */
    uid_copy = *proc_uid;
    param_copy = *param;

    ML_$LOCK(PROC2_LOCK_ID);

    index = PROC2_$FIND_INDEX(&uid_copy, &status);

    /* Get current process info */
    cur_index = P2_PID_TO_INDEX(PROC1_$CURRENT);
    cur_info = P2_INFO_ENTRY(cur_index);
    target_info = P2_INFO_ENTRY(index);

    /* Accept if process found or is zombie */
    if (status == 0 || status == status_$proc2_zombie) {
        /*
         * Permission check:
         * 1. Same parent as us (sibling) - allowed
         * 2. Same session and signal is SIGCONT (0x16) and session is valid - allowed
         * 3. Otherwise, check ACL fault rights
         */
        int permission_ok = 0;

        /* Check if same parent */
        if (P2_PARENT_FIELD(index) == P2_CHILD_IDX_FIELD(cur_index)) {
            permission_ok = 1;
        }
        /* Check same session with SIGCONT */
        else if (P2_SESSION_ID(index) == P2_SESSION_ID(cur_index) &&
                 *signal == SIGCONT &&
                 P2_SESSION_ID(index) != 0) {
            permission_ok = 1;
        }

        if (!permission_ok) {
            /* ACL check - negative result means permission granted */
            acl_result = ACL_$CHECK_FAULT_RIGHTS(
                (int16_t)(cur_index * 0xE4 + 0x54D2),
                (int16_t)(index * 0xE4 + 0x54D2));

            if (acl_result >= 0) {
                status = status_$proc2_permission_denied;
                goto done;
            }
        }

        /* Permission granted - deliver signal if not zombie */
        if (status == 0) {
            PROC2_$DELIVER_SIGNAL_INTERNAL(index, *signal, param_copy, &status);
        }
    }

done:
    ML_$UNLOCK(PROC2_LOCK_ID);

    *status_ret = status;

    /* Log the signal event */
    log_signal_event(1, index, *signal, param_copy, status);
}
