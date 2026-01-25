/*
 * PROC2_$WAIT - Wait for child process
 *
 * Waits for a child process to change state (exit, stop, etc.).
 * This is the Domain/OS implementation of the Unix wait() family.
 *
 * Parameters:
 *   options    - Pointer to options word (bit 0 = WNOHANG)
 *   pid        - Pointer to pid selector:
 *                  -1 = wait for any child
 *                   0 = wait for children in same pgroup
 *                  >0 = wait for specific pid (must be 65-30000)
 *                  <0 = wait for children in pgroup abs(pid)
 *   result     - Pointer to result buffer (104 bytes)
 *   status_ret - Returns status
 *
 * Returns:
 *   PID of waited process, or 0 if WNOHANG and no child ready
 *
 * Status codes:
 *   0 = success
 *   status_$proc2_wait_found_no_children (0x19000D) = no matching children
 *   status_$ec2_async_fault_while_waiting (0x180003) = interrupted by signal
 *
 * Original address: 0x00e3fdd0
 */

#include "proc2/proc2_internal.h"

/* WNOHANG option bit */
#define WNOHANG 0x0001

/* Status codes */
#define status_$proc2_wait_found_no_children    0x0019000D
#define status_$ec2_async_fault_while_waiting   0x00180003

/*
 * Raw memory access macros for wait-related fields
 */
#if defined(M68K)
    #define P2_W_BASE(idx)             ((uint8_t*)(0xEA551C + ((idx) * 0xE4)))

    /* Child/zombie list fields */
    #define P2_W_CHILD_HEAD(idx)       (*(int16_t*)(0xEA5458 + (idx) * 0xE4))
    #define P2_W_ZOMBIE_HEAD(idx)      (*(int16_t*)(0xEA545C + (idx) * 0xE4))
    #define P2_W_NEXT_SIBLING(idx)     (*(int16_t*)(0xEA545A + (idx) * 0xE4))
    #define P2_W_NEXT_ZOMBIE(idx)      (*(int16_t*)(0xEA5460 + (idx) * 0xE4))

    /* Process info fields */
    #define P2_W_PRIORITY(idx)         (*(int16_t*)(0xEA5450 + (idx) * 0xE4))
    #define P2_W_CHILD_PRIO(idx)       (*(int16_t*)(0xEA5452 + (idx) * 0xE4))
    #define P2_W_PGROUP(idx)           (*(int16_t*)(0xEA5448 + (idx) * 0xE4))
    #define P2_W_UPID(idx)             (*(int16_t*)(0xEA544E + (idx) * 0xE4))
    #define P2_W_SELF_IDX(idx)         (*(int16_t*)(0xEA5454 + (idx) * 0xE4))
    #define P2_W_ASID(idx)             (*(int16_t*)(0xEA54CE + (idx) * 0xE4))

    /* EC array access */
    #define W_CR_REC_EC(idx)           ((ec_$eventcount_t*)(0xE2B96C + (idx) * 24))
    #define W_FIM_QUIT_EC(asid)        ((ec_$eventcount_t*)(0xE22002 + (asid) * 12))
    #define W_FIM_QUIT_VAL(asid)       (*(int32_t*)(0xE222BA + (asid) * 4))
#else
    static int16_t p2_w_dummy16;
    #define P2_W_CHILD_HEAD(idx)       (p2_w_dummy16)
    #define P2_W_ZOMBIE_HEAD(idx)      (p2_w_dummy16)
    #define P2_W_NEXT_SIBLING(idx)     (p2_w_dummy16)
    #define P2_W_NEXT_ZOMBIE(idx)      (p2_w_dummy16)
    #define P2_W_PRIORITY(idx)         (p2_w_dummy16)
    #define P2_W_CHILD_PRIO(idx)       (p2_w_dummy16)
    #define P2_W_PGROUP(idx)           (p2_w_dummy16)
    #define P2_W_UPID(idx)             (p2_w_dummy16)
    #define P2_W_SELF_IDX(idx)         (p2_w_dummy16)
    #define P2_W_ASID(idx)             (p2_w_dummy16)
    #define W_CR_REC_EC(idx)           ((ec_$eventcount_t*)0)
    #define W_FIM_QUIT_EC(asid)        ((ec_$eventcount_t*)0)
    #define W_FIM_QUIT_VAL(asid)       (p2_w_dummy16)
#endif

int16_t PROC2_$WAIT(uint16_t *options, int16_t *pid, uint32_t *result,
                    status_$t *status_ret)
{
    int16_t cur_idx;
    int16_t child_idx;
    int16_t prev_idx;
    int16_t target_pgroup;
    int16_t pid_val;
    uint16_t opt;
    int16_t ret_pid;
    int8_t wait_any;
    int8_t wait_specific;
    int8_t wait_pgroup;
    int8_t found_matching;
    int8_t found;
    ec_$eventcount_t *ec_array[2];
    int32_t ec_vals[2];

    ret_pid = -1;
    *status_ret = 0;

    /* Clear result flag at offset 0x64 */
    ((uint8_t*)result)[0x64] = 0;

    opt = *options;
    pid_val = *pid;

    /* Get current process index */
    cur_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);

    /* Determine wait mode */
    wait_any = 0;
    wait_specific = 0;
    wait_pgroup = 0;
    target_pgroup = 0;

    if (pid_val == -1) {
        /* Wait for any child */
        wait_any = -1;
    } else if (pid_val > 0) {
        /* Wait for specific pid - but validate range */
        wait_specific = -1;
        /* Note: specific pid stored in pid_val, used below */
    } else if (pid_val == 0) {
        /* Wait for children in same pgroup */
        wait_pgroup = -1;
        target_pgroup = P2_W_PGROUP(cur_idx);
    } else {
        /* Wait for children in specific pgroup (pid_val is negative) */
        wait_pgroup = -1;
        target_pgroup = PGROUP_FIND_BY_UPGID(pid_val < 0 ? -pid_val : pid_val);
    }

    /* Validate specific pid range (65-30000) */
    if (pid_val > 0) {
        if (pid_val > 30000 || pid_val < 65) {
            *status_ret = status_$proc2_wait_found_no_children;
            return ret_pid;
        }
    }

    /* Main wait loop */
    while (1) {
        /* Check if we have any children */
        if (P2_W_CHILD_HEAD(cur_idx) == 0 && P2_W_ZOMBIE_HEAD(cur_idx) == 0) {
            *status_ret = status_$proc2_wait_found_no_children;
            return ret_pid;
        }

        ML_$LOCK(PROC2_LOCK_ID);

        found_matching = 0;
        prev_idx = 0;

        /* Search live children */
        child_idx = P2_W_CHILD_HEAD(cur_idx);
        while (child_idx != 0) {
            /* Check priority match and pid/pgroup filter */
            if (P2_W_PRIORITY(cur_idx) == P2_W_CHILD_PRIO(child_idx)) {
                int match = 0;

                if (wait_any < 0) {
                    match = 1;
                } else if (wait_specific < 0 && pid_val == P2_W_UPID(child_idx)) {
                    match = 1;
                } else if (wait_pgroup < 0 && target_pgroup == P2_W_PGROUP(child_idx)) {
                    match = 1;
                }

                if (match) {
                    found_matching = -1;

                    /* Try to collect status from this child */
                    PROC2_$WAIT_TRY_LIVE_CHILD(child_idx, opt, cur_idx, prev_idx,
                                               &found, result, &ret_pid);

                    if (found < 0) {
                        /* Found a child that changed state */
                        ML_$UNLOCK(PROC2_LOCK_ID);
                        return ret_pid;
                    }
                }
            }

            prev_idx = child_idx;
            child_idx = P2_W_NEXT_SIBLING(child_idx);
        }

        /* Search zombie children */
        child_idx = P2_W_ZOMBIE_HEAD(cur_idx);
        while (child_idx != 0) {
            /* Check pid/pgroup filter (no priority check for zombies) */
            int match = 0;

            if (wait_any < 0) {
                match = 1;
            } else if (wait_specific < 0 && pid_val == P2_W_UPID(child_idx)) {
                match = 1;
            } else if (wait_pgroup < 0 && target_pgroup == P2_W_PGROUP(child_idx)) {
                match = 1;
            }

            if (match) {
                found_matching = -1;

                /* Collect status from zombie */
                PROC2_$WAIT_TRY_ZOMBIE(child_idx, opt, &found, result, &ret_pid);

                if (found < 0) {
                    /* Collected zombie status */
                    ML_$UNLOCK(PROC2_LOCK_ID);
                    return ret_pid;
                }
            }

            child_idx = P2_W_NEXT_ZOMBIE(child_idx);
        }

        /* Set up eventcounts for waiting */
        int16_t self_idx = P2_W_SELF_IDX(cur_idx);
        int16_t asid = P2_W_ASID(cur_idx);

        ec_array[0] = W_CR_REC_EC(self_idx);
        ec_array[1] = W_FIM_QUIT_EC(asid);

        ec_vals[0] = EC_$READ(ec_array[0]) + 1;
        ec_vals[1] = W_FIM_QUIT_VAL(asid) + 1;

        ML_$UNLOCK(PROC2_LOCK_ID);

        /* If no matching children found, return error */
        if (found_matching >= 0) {
            *status_ret = status_$proc2_wait_found_no_children;
            return ret_pid;
        }

        /* WNOHANG - return immediately if no child ready */
        if ((opt & WNOHANG) != 0) {
            ret_pid = 0;
            return ret_pid;
        }

        /* Block waiting for child state change or signal */
        int16_t which = EC_$WAITN(ec_array, ec_vals, 2);

        if (which == 2) {
            /* Interrupted by signal */
#if defined(M68K)
            W_FIM_QUIT_VAL(asid) = *(int32_t*)W_FIM_QUIT_EC(asid);
#endif
            *status_ret = status_$ec2_async_fault_while_waiting;
            return ret_pid;
        }

        /* Loop back to check children again */
    }
}
