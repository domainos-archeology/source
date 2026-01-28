/*
 * PROC2_$SIGNAL_PGROUP_INTERNAL - Internal helper for process group signaling
 *
 * Iterates through all processes in the allocation list, signaling
 * those that belong to the specified process group.
 *
 * Permission checking is controlled by the check_perms parameter:
 *   - If check_perms < 0 (0xFF): ACL check required
 *   - If check_perms >= 0 (0): No permission check
 *
 * SIGCONT (signal 0x16/22) is a special case that bypasses ACL checks
 * if the target is in the same session.
 *
 * Parameters:
 *   pgroup_idx   - Process group index
 *   signal       - Signal number to send
 *   param        - Signal parameter
 *   check_perms  - If negative, perform permission checking
 *   status_ret   - Returns status (0 on success)
 *
 * Original address: 0x00e3f160
 */

#include "proc2/proc2_internal.h"

/*
 * Raw memory access for process group index field
 * At offset -0xD4 from entry end = field at 0x10 area - pgroup index
 */
#if defined(ARCH_M68K)
    #define P2_PGROUP_BASE(idx)     ((int16_t*)(0xEA551C + ((idx) * 0xE4)))
    #define P2_PGROUP_IDX(idx)      (*(P2_PGROUP_BASE(idx) - 0x6A))
    #define P2_FLAGS_FIELD(idx)     (*(uint16_t*)(0xEA5462 + (idx) * 0xE4))
    #define P2_NEXT_IDX(idx)        (*(int16_t*)(0xEA544A + (idx) * 0xE4))
    #define P2_SESSION_ID2(idx)     (*(int16_t*)(0xEA5494 + (idx) * 0xE4))
#else
    static int16_t p2_pgroup_dummy;
    static uint16_t p2_flags_dummy;
    #define P2_PGROUP_IDX(idx)      (p2_pgroup_dummy)
    #define P2_FLAGS_FIELD(idx)     (p2_flags_dummy)
    #define P2_NEXT_IDX(idx)        (p2_pgroup_dummy)
    #define P2_SESSION_ID2(idx)     (p2_pgroup_dummy)
#endif

void PROC2_$SIGNAL_PGROUP_INTERNAL(int16_t pgroup_idx, int16_t signal,
                                    uint32_t param, int8_t check_perms,
                                    status_$t *status_ret)
{
    int16_t cur_idx;
    int8_t signaled_any;
    int8_t all_zombies;
    int8_t acl_result;
    status_$t status;
    uint32_t param_copy;

    /* Handle invalid pgroup index */
    if (pgroup_idx == 0) {
        *status_ret = status_$proc2_uid_not_found;
        PROC2_$LOG_SIGNAL_EVENT(2, pgroup_idx, signal, param, *status_ret);
        return;
    }

    *status_ret = 0;
    signaled_any = 0;
    all_zombies = 0;
    param_copy = param;

    /* Iterate through allocation list */
    cur_idx = P2_INFO_ALLOC_PTR;

    while (cur_idx != 0) {
        /* Check if this process belongs to the target pgroup */
        if (P2_PGROUP_IDX(cur_idx) == pgroup_idx) {

            /* Check if process is a zombie (flag 0x2000) */
            if ((P2_FLAGS_FIELD(cur_idx) & PROC2_FLAG_ZOMBIE) != 0) {
                /* Mark that we encountered a zombie */
                all_zombies = -1;
            } else {
                /* Process is alive - check permissions if required */
                int permission_ok = 1;

                if (check_perms < 0) {
                    /* ACL check required */
                    acl_result = ACL_$CHECK_FAULT_RIGHTS(
                        0x0608,  /* Source offset - current process? */
                        (int16_t)(cur_idx * 0xE4 + 0x54D2));

                    if (acl_result >= 0) {
                        /* Permission denied - but check SIGCONT special case */
                        if (signal == SIGCONT &&
                            P2_SESSION_ID2(cur_idx) == P2_SESSION_ID2(cur_idx)) {
                            /* Same session with SIGCONT - allowed */
                            permission_ok = 1;
                        } else {
                            permission_ok = 0;
                            *status_ret = status_$proc2_permission_denied;
                        }
                    }
                }

                if (permission_ok) {
                    /* Deliver the signal */
                    PROC2_$DELIVER_SIGNAL_INTERNAL(cur_idx, signal, param_copy, &status);
                    signaled_any = -1;
                }
            }
        }

        /* Move to next process in allocation list */
        cur_idx = P2_NEXT_IDX(cur_idx);
    }

    /* Determine final status */
    if (signaled_any < 0) {
        /* Successfully signaled at least one process */
        /* status_ret already 0 or set to permission_denied for partial success */
        goto done;
    }

    if (all_zombies < 0) {
        /* All processes in group were zombies */
        *status_ret = status_$proc2_zombie;
        goto done;
    }

    /* No processes found in group */
    *status_ret = status_$proc2_uid_not_found;

done:
    PROC2_$LOG_SIGNAL_EVENT(2, pgroup_idx, signal, param, *status_ret);
}
