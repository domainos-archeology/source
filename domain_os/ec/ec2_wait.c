/*
 * EC2_$WAIT - Wait on a list of level-2 eventcounts
 *
 * The SVC_$TRAP4_TABLE[0x04] entry.  Takes a list of up to 0x80 EC2
 * "handles" - each a longword that is either the address of an
 * ec2_$eventcount_t (above 0x3E8 / above 0x120 here) or an EC2 index
 * (1..max registered, or 0x101..0x120 for the PBU pool) - with a parallel
 * list of wait values, and blocks in EC_$WAITN until one of them is
 * satisfied, the calling process's quit eventcount fires, or a fault unwinds
 * through the FIM cleanup handler.
 *
 * Per round (0x00E423C6-0x00E427AE) the list is scanned under ML lock 6:
 *   - index handles resolve to their level-1 eventcount, which is appended
 *     to the EC_$WAITN list (at most 0x20 entries, two of which are the
 *     per-process wait eventcount and the quit eventcount);
 *   - address handles get an EC2 waiter record (EC2_WAITER_TABLE_BASE)
 *     threaded onto the eventcount's ring and recorded in waiter_idx[];
 * then the lock is dropped, EC_$WAITN blocks, the lock is retaken and a
 * backwards cleanup pass (0x00E4266C-0x00E427A6) unthreads every waiter,
 * drops PBU reference counts and re-tests EVERY entry, so the result is the
 * LOWEST satisfied index.  If nothing is satisfied the round repeats.
 *
 * Parameters:
 *   ecs        - array of EC2 handles, longwords (argument 1, (0x8,A6))
 *   wait_vals  - array of wait values (argument 2, (0xC,A6))
 *   count      - pointer to the list length word (argument 3, (0x10,A6))
 *   status_ret - status return (argument 4, (0x14,A6)), by reference
 *
 * Returns:
 *   1-based index of the satisfied (or faulting) entry; 0 for a quit
 *   (status_$ec2_async_fault_while_waiting) or a rejected count.
 *
 * Original address: 0x00e42358 (map: EC2, 0xE42358 size 0x908)
 * Re-emitted from the disassembly 0x00E42358-0x00E42858.
 */

#include "ec/ec_internal.h"
#include "fim/fim.h"
#include "proc1/proc1.h"
#include "ml/ml.h"

/* Longest list accepted: `cmpi.w #0x80,(-0x254,A6) / ble` (0x00E4236E). */
#define EC2_WAIT_MAX_COUNT   0x80

/* EC_$WAITN list capacity: `cmpi.w #0x20,D4w / blt` (0x00E4248A). */
#define EC2_WAIT_MAX_EC1S    0x20

int16_t EC2_$WAIT(ec2_$eventcount_t **ecs, int32_t *wait_vals,
                  int16_t *count, status_$t *status_ret)
{
    int16_t   n_req;                          /* (-0x254,A6)              */
    int16_t   result = 0;                     /* (-0x252,A6): see exit    */
    int16_t   n_filled;                       /* (-0x120,A6)              */
    uint16_t  waiter_idx[EC2_WAIT_MAX_COUNT]; /* (-0x11E,A6) .. (-0x1E)   */
    uint8_t   cleanup_rec[0x18];              /* (-0x18,A6)               */
    status_$t cleanup_status;                 /* (-0x234,A6)              */
    ec_$eventcount_t *ec1s[EC2_WAIT_MAX_EC1S];/* (-0x230,A6)              */
    int32_t   ec1_vals[EC2_WAIT_MAX_EC1S];    /* (-0x1A8,A6)              */
    int16_t   satisfied;                      /* (-0x24E,A6)              */
    int16_t   scan_left;                      /* (-0x256,A6)              */
    int16_t   last;                           /* D5: n_req - 1            */
    int16_t   n_ec1;                          /* D4                       */
    uint16_t  cur_pid;                        /* D3: PROC1_$CURRENT       */
    int16_t   i;                              /* D2 / D0: 1-based entry   */
    uint32_t  handle;                         /* D6 / D7                  */
    ec_$eventcount_t *ec1;                    /* D0 in the scan           */
    ec2_$eventcount_t *ec2;                   /* A2 / A1                  */
    uint16_t  pbu_slot;
    uint16_t  old_head, widx, next_head;      /* D1 / D0 / D2             */
    ec2_waiter_t *w, *ow;                     /* A1 / A0                  */
    uint16_t  as_id;
    uint16_t  waitn_result;
    int32_t   value;

    /* 0x00E42366-0x00E42384: n = *count; more than 0x80 (signed) is
     * status_$ec2_internal_table_exhausted with result 0. */
    n_req = *count;
    if (n_req > EC2_WAIT_MAX_COUNT) {
        *status_ret = status_$ec2_internal_table_exhausted;   /* 0x180001 */
        result = 0;
        return result;
    }

    /* 0x00E42388-0x00E4238E */
    *status_ret = status_$ok;
    n_filled = 0;

    /* 0x00E42392-0x00E423A8: arm the cleanup handler. */
    cleanup_status = FIM_$CLEANUP(cleanup_rec);
    if (cleanup_status != status_$cleanup_handler_set) {
        /*
         * 0x00E427D2-0x00E42846: a fault unwound through here.  Give every
         * waiter record recorded so far back to the free list (ascending,
         * n_filled iterations of `dbf D1w`), without touching the
         * eventcounts' waiter words, then unlock and re-signal.
         */
        for (i = 0; i < n_filled; i++) {
            widx = waiter_idx[i];
            /* 0x00E427DE-0x00E427E8: 0 or above 0xE0 is skipped */
            if (widx == 0 || widx > 0xE0) {
                continue;
            }
            w = &EC2_WAITER_TABLE_BASE[widx];
            /* 0x00E427F8-0x00E42816: unthread from the ring */
            EC2_WAITER_TABLE_BASE[(uint16_t)w->next].prev = w->prev;
            EC2_WAITER_TABLE_BASE[(uint16_t)w->prev].next = w->next;
            /* 0x00E4281C-0x00E4282A: push on the free list */
            w->prev    = 0;
            w->proc_id = 0;
            w->next    = (int16_t)DAT_00e7cf08;
            DAT_00e7cf08 = widx;
        }
        ML_$UNLOCK(EC2_LOCK_ID);
        FIM_$SIGNAL(cleanup_status);
        /* 0x00E4284C: FIM_$SIGNAL does not return; the image would hand
         * back whatever is in the result slot.  It is left at 0 here. */
        return result;
    }

    /* 0x00E423AC-0x00E423B8: ML_$LOCK(6). */
    ML_$LOCK(EC2_LOCK_ID);

    /* 0x00E423BA-0x00E423C4 */
    satisfied = -1;
    last = (int16_t)(n_req - 1);

    for (;;) {
        /* 0x00E423C6-0x00E423D0: round start.  D4 and D3 are reloaded
         * every round, D5 (last) and the satisfied slot are not. */
        n_ec1   = 2;
        cur_pid = PROC1_$CURRENT;

        if (last >= 0) {
            /* 0x00E423D4-0x00E42406: scan cursors. */
            scan_left = last;
            i = 1;

            do {
                /* 0x00E4240A-0x00E4241A: fetch the handle. */
                handle = ARCH_PTR_TO_VA(ecs[i - 1]);

                if (handle > 0x120) {
                    /*
                     * 0x00E424C2-0x00E424D4: an eventcount address.  It must
                     * lie below AS_$PROTECTION (unsigned).
                     */
                    if (handle >= ARCH_PTR_TO_VA(AS_$PROTECTION)) {
                        *status_ret = status_$fault_protection_boundary_violation;
                        goto scan_stop;
                    }
                    ec2 = ecs[i - 1];

                    /* 0x00E424D8-0x00E424DC: take the waiter word, mark it
                     * busy (-1) while the record is threaded. */
                    old_head      = (uint16_t)ec2->awaiters;
                    ec2->awaiters = -1;

                    /* 0x00E424E2-0x00E424F8: free-list head; empty means
                     * table exhausted - restore the word and stop. */
                    widx = DAT_00e7cf08;
                    if (widx == 0) {
                        *status_ret = status_$ec2_internal_table_exhausted;
                        ec2->awaiters = (int16_t)old_head;
                        goto scan_stop;
                    }

                    /* 0x00E424FC-0x00E42506: w = &table[widx] */
                    w = &EC2_WAITER_TABLE_BASE[widx];

                    if (old_head == 0) {
                        /* 0x00E4250E-0x00E4251E: first waiter - a ring of
                         * one, and the eventcount will point at it. */
                        DAT_00e7cf08 = (uint16_t)w->next;
                        old_head     = widx;
                        w->next      = (int16_t)widx;
                        w->prev      = (int16_t)widx;
                    } else {
                        /* 0x00E42520-0x00E4253A: the existing head must be a
                         * live record (index <= 0xE0, proc_id != 0). */
                        if (old_head > 0xE0) {
                            *status_ret = status_$ec2_bad_event_count;
                            ec2->awaiters = (int16_t)old_head;
                            goto scan_stop;
                        }
                        ow = &EC2_WAITER_TABLE_BASE[old_head];
                        if (ow->proc_id == 0) {
                            /* 0x00E4253C-0x00E42546 -> 0x00E424F4 */
                            *status_ret = status_$ec2_bad_event_count;
                            ec2->awaiters = (int16_t)old_head;
                            goto scan_stop;
                        }
                        /* 0x00E42548-0x00E42568: insert after the head. */
                        DAT_00e7cf08 = (uint16_t)w->next;
                        w->prev  = (int16_t)old_head;
                        w->next  = ow->next;
                        EC2_WAITER_TABLE_BASE[(uint16_t)ow->next].prev = (int16_t)widx;
                        ow->next = (int16_t)widx;
                    }

                    /* 0x00E4256C-0x00E42584: fill the record, publish it. */
                    w->proc_id        = (int16_t)cur_pid;
                    n_filled          = i;
                    waiter_idx[i - 1] = widx;
                    ec2->awaiters     = (int16_t)old_head;
                    w->wait_val       = wait_vals[i - 1];

                    /* 0x00E42588-0x00E4258C: already satisfied? */
                    if ((int32_t)(ec2->value - w->wait_val) >= 0) {
                        goto scan_stop;
                    }
                } else {
                    /* 0x00E4241E-0x00E4247A: an EC2 index. */
                    if (handle == 0) {
                        /* 0x00E42422-0x00E4242E */
                        *status_ret = status_$ec2_bad_event_count;
                        goto scan_stop;
                    }
                    if (handle == 1) {
                        /* 0x00E42432-0x00E42438: index 1 counts as
                         * satisfied at once, status untouched. */
                        goto scan_stop;
                    }
                    if (handle <= _DAT_00e7cf04) {
                        /* 0x00E4243C-0x00E4244C: a registered EC1. */
                        ec1 = (ec_$eventcount_t *)DAT_00e7caf8[handle];
                    } else if (handle < 0x101) {
                        /* 0x00E4244E-0x00E42454 -> 0x00E42424 */
                        *status_ret = status_$ec2_bad_event_count;
                        goto scan_stop;
                    } else {
                        /* 0x00E42456-0x00E42462: PBU slot must be marked
                         * allocated. */
                        pbu_slot = (uint16_t)(handle - 0x101);
                        if ((DAT_00e7cf00 & ((uint32_t)1 << (pbu_slot & 0x1F))) == 0) {
                            *status_ret = status_$ec2_bad_event_count;
                            goto scan_stop;
                        }
                        /* 0x00E42464-0x00E4247A: record address, and bump
                         * its waiter reference word. */
                        ec1 = (ec_$eventcount_t *)
                              (EC2_$PBU_ECS + (uint16_t)(pbu_slot * EC2_PBU_EC_SIZE));
                        (*(int16_t *)((uint8_t *)ec1 + 0x0E))++;
                    }

                    /* 0x00E4247E-0x00E42486: this entry has no waiter
                     * record. */
                    n_filled          = i;
                    waiter_idx[i - 1] = 0;

                    /* 0x00E4248A-0x00E4249A: room in the EC_$WAITN list? */
                    if (n_ec1 >= EC2_WAIT_MAX_EC1S) {
                        *status_ret = status_$ec2_internal_table_exhausted;
                        goto scan_stop;
                    }

                    /* 0x00E4249E-0x00E424BE: append. */
                    ec1s[n_ec1]     = ec1;
                    ec1_vals[n_ec1] = wait_vals[i - 1];
                    n_ec1++;
                }

                /* 0x00E42596-0x00E425AC: next entry; `subq.w #1 / bcc`
                 * runs the body n_req times. */
                i++;
                scan_left--;
            } while (scan_left >= 0);
        }

        /*
         * 0x00E425B0-0x00E425CE: ec1s[0] is this process's wait
         * eventcount, EC2_$WAIT_ECS + PROC1_$CURRENT * 0x0C - 0x0C
         * (`lea (-0xc,A1,D0w*0x1),A2` off 0xE20F6C), waited for one past
         * its current value.
         */
        ec1s[0]     = (ec_$eventcount_t *)
                      (EC2_$WAIT_ECS + (uint16_t)(cur_pid * EC2_WAIT_EC_SIZE) - EC2_WAIT_EC_SIZE);
        ec1_vals[0] = ec1s[0]->value + 1;

        /* 0x00E425D2-0x00E425DE: ML_$UNLOCK(6). */
        ML_$UNLOCK(EC2_LOCK_ID);

        /* 0x00E425E0-0x00E42610: ec1s[1] is the address space's quit
         * eventcount (0xE22002, stride 12), waited for one past the
         * cached FIM_$QUIT_VALUE (0xE222BA, stride 4). */
        as_id       = PROC1_$AS_ID;
        ec1s[1]     = &FIM_$WIRED_DATA.quit_ec[as_id];
        ec1_vals[1] = (int32_t)(FIM_$WIRED_DATA.quit_value[as_id] + 1);

        /* 0x00E42614-0x00E42626: EC_$WAITN(&ec1s, &ec1_vals, n_ec1). */
        waitn_result = EC_$WAITN(ec1s, ec1_vals, n_ec1);

        /* 0x00E4262A-0x00E4265A: `cmpi.w #0x2,D0w / seq` - the quit
         * eventcount (1-based entry 2) woke us: refresh the cached quit
         * value from the eventcount, report the async fault, result 0. */
        if (waitn_result == 2) {
            FIM_$WIRED_DATA.quit_value[PROC1_$AS_ID] = (uint32_t)FIM_$WIRED_DATA.quit_ec[PROC1_$AS_ID].value;
            *status_ret = status_$ec2_async_fault_while_waiting;   /* 0x180003 */
            satisfied = 0;
        }

        /* 0x00E4265E-0x00E4266A: ML_$LOCK(6), then fall into the cleanup
         * pass at 0x00E4266C. */
        ML_$LOCK(EC2_LOCK_ID);
        goto cleanup;

scan_stop:
        /* 0x00E4258E-0x00E42592: the scan stopped at entry i - it is the
         * provisional answer, and the cleanup pass runs without any wait. */
        satisfied = i;

cleanup:
        /*
         * 0x00E4266C-0x00E427A6: cleanup pass, entries n_filled down to 1
         * (`dbf D1w` on n_filled - 1).  It does NOT stop at the first
         * satisfied entry, so the lowest index wins.
         */
        for (i = n_filled; i >= 1; i--) {
            ec2  = ecs[i - 1];
            widx = waiter_idx[i - 1];

            if (widx != 0) {
                /* 0x00E4271E-0x00E4272E: an address handle with a waiter
                 * record - forget it, unthread it. */
                waiter_idx[i - 1] = 0;
                w = &EC2_WAITER_TABLE_BASE[widx];

                if (w->next == (int16_t)widx) {
                    /* 0x00E42762: the ring was just us. */
                    next_head = 0;
                } else {
                    /* 0x00E42738-0x00E4275C */
                    EC2_WAITER_TABLE_BASE[(uint16_t)w->prev].next = w->next;
                    EC2_WAITER_TABLE_BASE[(uint16_t)w->next].prev = w->prev;
                    next_head = (uint16_t)w->next;
                }

                /* 0x00E42764-0x00E4276E: back on the free list. */
                w->next      = (int16_t)DAT_00e7cf08;
                DAT_00e7cf08 = widx;
                w->proc_id   = 0;

                /* 0x00E42772-0x00E42784: the handle is re-checked against
                 * AS_$PROTECTION before the eventcount is touched. */
                if (ARCH_PTR_TO_VA(ec2) >= ARCH_PTR_TO_VA(AS_$PROTECTION)) {
                    *status_ret = status_$fault_protection_boundary_violation;
                    satisfied = i;
                    continue;
                }

                /* 0x00E42786-0x00E4278A */
                ec2->awaiters = (int16_t)next_head;
                value = ec2->value;
            } else {
                /* 0x00E426BA-0x00E4271C: an index handle. */
                handle = ARCH_PTR_TO_VA(ec2);
                if (handle >= 0x101 && handle <= 0x120) {
                    /* 0x00E426CC-0x00E426EE: drop the PBU reference, then
                     * require the slot to still be allocated. */
                    pbu_slot = (uint16_t)(handle - 0x101);
                    ec1 = (ec_$eventcount_t *)
                          (EC2_$PBU_ECS + (uint16_t)(pbu_slot * EC2_PBU_EC_SIZE));
                    (*(int16_t *)((uint8_t *)ec1 + 0x0E))--;
                    if ((DAT_00e7cf00 & ((uint32_t)1 << (pbu_slot & 0x1F))) == 0) {
                        /* 0x00E426F8-0x00E42702 */
                        *status_ret = status_$ec2_bad_event_count;
                        satisfied = i;
                        continue;
                    }
                    value = ec1->value;
                } else if (handle == 0 || handle > _DAT_00e7cf04) {
                    /* 0x00E42706-0x00E4270E -> 0x00E426F8 */
                    *status_ret = status_$ec2_bad_event_count;
                    satisfied = i;
                    continue;
                } else {
                    /* 0x00E42710-0x00E4271A: registered EC1 (index 1 reads
                     * the registration-count cell). */
                    value = ((ec_$eventcount_t *)DAT_00e7caf8[handle])->value;
                }
            }

            /* 0x00E4278C-0x00E42794: satisfied when value - wait >= 0. */
            if ((int32_t)(value - wait_vals[i - 1]) >= 0) {
                satisfied = i;
            }
        }

        /* 0x00E427AA-0x00E427AE: nothing yet - go round again. */
        if (satisfied < 0) {
            continue;
        }
        break;
    }

    /* 0x00E427B2-0x00E427D0: result, unlock, release the handler. */
    result = satisfied;
    ML_$UNLOCK(EC2_LOCK_ID);
    FIM_$RLS_CLEANUP(cleanup_rec);

    /* 0x00E4284C-0x00E42858 */
    return result;
}
