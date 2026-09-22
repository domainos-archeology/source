/*
 * PROC2_$WAIT - Wait for a child to stop or exit (wait4)
 *
 * Re-emitted from the image (0x00E3FDD0..0x00E400C0, 754 bytes).
 *
 * Frame (link.w A6,-0xA4; A5 = 0xE7BE84):
 *   (0x8,A6)  options ptr -> D4 = *ptr (bit 0: don't block; bit 1 passed on)
 *   (0xC,A6)  pid ptr -> A3, re-read at each use
 *   (0x10,A6) result: the 0x68-byte record; its byte 0x64 is cleared at
 *             entry (0x00E3FDF6) and the whole record is copied from the
 *             local at A6-0x68 only on success
 *   (0x14,A6) status_ret (cleared at 0x00E3FDEC)
 *   A6-0x8A   the returned pid (D0 at exit), preset to -1
 *   A6-0x96/-0x94/-0x92  wait_any / wait_specific / wait_pgroup booleans
 *   A6-0x7C   target group index; D3 target upid
 *   A6-0x90   "some child qualified"; A6-0x82 previous sibling
 *   A6-0x8E / A6-0x8C  found flags; A6-0x80 pid from the helpers
 *   A6-0x78   {cr_rec EC, quit EC}; A6-0x70 {value+1, quit value+1}
 *   D6 caller's index, A2 caller's entry (biased)
 *
 * The second walk (0x00E3FF4E) is over the caller's +0x24 / +0x28 chain --
 * the fields proc2_info_t names first_debug_target_idx /
 * next_debug_target_idx -- through PROC2_$WAIT_TRY_ZOMBIE.
 *
 * Only reference: the SVC table entry at 0x00E7BA46.
 *
 * Original address: 0x00e3fdd0
 */

#include "proc2/proc2_internal.h"

int16_t PROC2_$WAIT(uint16_t *options, int16_t *pid, void *result,
                    status_$t *status_ret)
{
    proc2_wait_result_t local;   /* A6-0x68 */
    int16_t ret_pid;             /* A6-0x8A */
    uint16_t opt;                /* D4 */
    int16_t cur_idx;             /* D6 */
    proc2_info_t *current;       /* A2 */
    proc2_info_t *child;         /* A3 */
    int8_t wait_any, wait_specific, wait_pgroup;   /* A6-0x96/-0x94/-0x92 */
    int16_t target_upid;         /* D3 */
    int16_t target_pgroup;       /* A6-0x7C */
    int8_t qualified;            /* A6-0x90 */
    int16_t prev;                /* A6-0x82 */
    int8_t found;                /* A6-0x8E / A6-0x8C */
    int16_t helper_pid;          /* A6-0x80 */
    int16_t idx;                 /* D2 */
    ec_$eventcount_t *ecs[2];    /* A6-0x78 */
    int32_t vals[2];             /* A6-0x70 */
    int16_t which;
    int i;

    /* 0x00E3FDE2-0x00E3FDF6 */
    ret_pid = -1;
    *status_ret = status_$ok;
    ((proc2_wait_result_t *)result)->flag_64 = 0;

    /* 0x00E3FDFA-0x00E3FE22 */
    opt = *options;
    cur_idx = (int16_t)P2_PID_TO_INDEX(PROC1_$CURRENT);
    current = P2_INFO_ENTRY(cur_idx);

    /* 0x00E3FE26-0x00E3FE6C */
    wait_any = wait_specific = wait_pgroup = 0;
    if (*pid == -1) {
        wait_any = (int8_t)0xFF;
    } else if (*pid > 0) {
        wait_specific = (int8_t)0xFF;
        target_upid = *pid;
    } else if (*pid == 0) {
        wait_pgroup = (int8_t)0xFF;
        target_pgroup = (int16_t)current->pgroup_table_idx;  /* 0x00E3FE50 */
    } else {
        wait_pgroup = (int8_t)0xFF;
        /* 0x00E3FE5E-0x00E3FE6C: |*pid| (neg.w when negative) */
        target_pgroup = PGROUP_FIND_BY_UPGID((uint16_t)(*pid < 0 ? -*pid : *pid));
    }

    /* 0x00E3FE70-0x00E3FE80: a positive pid must be 0x41..30000 */
    if (*pid > 0 && (*pid > 30000 || *pid < 0x41)) {
        goto no_children;
    }

loop_top:
    /* 0x00E3FE84-0x00E3FE8E: neither list has a member */
    if (current->first_child_idx == 0 && current->first_debug_target_idx == 0) {
        goto no_children;
    }

    /* 0x00E3FE92-0x00E3FE9E */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E3FEA0-0x00E3FEA4 */
    qualified = 0;
    prev = 0;

    /* 0x00E3FEA8-0x00E3FF4A: the child list (+0x20 / +0x22) */
    idx = (int16_t)current->first_child_idx;
    while (idx != 0) {
        child = P2_INFO_ENTRY(idx);                          /* 0x00E3FEB0-0x00E3FEBC */
        /* 0x00E3FEC0-0x00E3FECA: caller+0x18 must equal child+0x1A */
        if (current->pad_18[0] == child->pad_18[1]) {
            /* 0x00E3FECC-0x00E3FEEC: the selector chain */
            int8_t match = 0;
            if (wait_any < 0) {
                match = (int8_t)0xFF;
            } else if (wait_specific < 0 && target_upid == (int16_t)child->upid) {
                match = (int8_t)0xFF;
            } else if (wait_pgroup < 0 && target_pgroup == (int16_t)child->pgroup_table_idx) {
                match = (int8_t)0xFF;
            }
            if (match < 0) {
                qualified = (int8_t)0xFF;                    /* 0x00E3FEEE */
                /* 0x00E3FEF2-0x00E3FF0C */
                PROC2_$WAIT_TRY_LIVE_CHILD(idx, opt, cur_idx, prev, &found, &local, &helper_pid);
                /* 0x00E3FF10-0x00E3FF18 */
                if (found < 0) {
                    ML_$UNLOCK(PROC2_LOCK_ID);               /* 0x00E3FF1A */
                    for (i = 0; i < 0x68; i++) {             /* 0x00E3FF28-0x00E3FF36 */
                        ((uint8_t *)result)[i] = ((const uint8_t *)&local)[i];
                    }
                    ret_pid = helper_pid;                    /* 0x00E3FF3A */
                    goto exit;
                }
            }
        }
        prev = idx;                                          /* 0x00E3FF42 */
        idx = (int16_t)child->next_child_sibling;            /* 0x00E3FF46 */
    }

    /* 0x00E3FF4E-0x00E3FFDA: the +0x24 / +0x28 chain */
    idx = (int16_t)current->first_debug_target_idx;
    while (idx != 0) {
        child = P2_INFO_ENTRY(idx);                          /* 0x00E3FF58-0x00E3FF64 */
        {
            int8_t match = 0;
            if (wait_any < 0) {
                match = (int8_t)0xFF;
            } else if (wait_specific < 0 && target_upid == (int16_t)child->upid) {
                match = (int8_t)0xFF;
            } else if (wait_pgroup < 0 && target_pgroup == (int16_t)child->pgroup_table_idx) {
                match = (int8_t)0xFF;
            }
            if (match < 0) {
                qualified = (int8_t)0xFF;                    /* 0x00E3FF8A */
                /* 0x00E3FF8E-0x00E3FFA2 */
                PROC2_$WAIT_TRY_ZOMBIE(idx, opt, &found, &local, &helper_pid);
                idx = helper_pid;                            /* 0x00E3FFA6: D2 = pid */
                if (found < 0) {                             /* 0x00E3FFAA */
                    ML_$UNLOCK(PROC2_LOCK_ID);               /* 0x00E3FFB0 */
                    for (i = 0; i < 0x68; i++) {             /* 0x00E3FFBE-0x00E3FFCA */
                        ((uint8_t *)result)[i] = ((const uint8_t *)&local)[i];
                    }
                    ret_pid = idx;                           /* 0x00E3FFCE */
                    goto exit;
                }
            }
        }
        idx = (int16_t)child->next_debug_target_idx;         /* 0x00E3FFD6 */
    }

    /* 0x00E3FFDE-0x00E40036: the two eventcounts and their targets */
    ecs[0] = PROC_CR_REC_EC(current->self_index);
    ecs[1] = &FIM_$QUIT_EC[current->asid];
    vals[0] = EC_$READ(ecs[0]) + 1;                          /* 0x00E40012-0x00E4001E */
    vals[1] = (int32_t)(FIM_$QUIT_VALUE[current->asid] + 1); /* 0x00E40024-0x00E40036 */

    /* 0x00E4003A-0x00E40046 */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E40048: nobody qualified */
    if (qualified >= 0) {
        goto no_children;
    }

    /* 0x00E4005A: btst #0 of the options */
    if ((opt & 0x0001) != 0) {
        ret_pid = 0;                                         /* 0x00E40060 */
        goto exit;
    }

    /* 0x00E40066-0x00E40082 */
    which = (int16_t)EC_$WAITN(ecs, vals, 2);
    if (which != 2) {
        goto loop_top;
    }

    /* 0x00E40086-0x00E400AE: a quit -- resync the value, report the fault */
    FIM_$QUIT_VALUE[current->asid] = (uint32_t)FIM_$QUIT_EC[current->asid].value;
    *status_ret = status_$ec2_async_fault_while_waiting;
    goto exit;

no_children:
    /* 0x00E4004E-0x00E40058 */
    *status_ret = status_$proc2_wait_found_no_children;

exit:
    /* 0x00E400B4: D0 = A6-0x8A */
    return ret_pid;
}
