/*
 * EC2_$WAKEUP - Wake the waiters of a level-2 eventcount
 *
 * Walks the ring of EC2 waiter records hanging off the eventcount's waiter
 * word and, for each record whose wait value has been reached, EC_$ADVANCEs
 * the waiting process's per-process wait eventcount
 * (EC2_$WAIT_ECS + proc_id * 0x0C - 0x0C).  The walk runs under ML lock 6
 * inside a FIM cleanup handler.
 *
 * Parameters:
 *   ec         - EC2 pointer (argument 1, (0x8,A6))
 *   status_ret - Status return (argument 2, (0xC,A6)), passed by reference:
 *                status_$ok, or status_$ec2_bad_event_count when the waiter
 *                word is above 0xE0 or names a free record
 *
 * Original address: 0x00e4285a (map: EC2, 0xE42358 size 0x908)
 * Re-emitted from the disassembly 0x00E4285A-0x00E4293A.
 */

#include "ec/ec_internal.h"
#include "fim/fim.h"
#include "ml/ml.h"

void EC2_$WAKEUP(ec2_$eventcount_t *ec, status_$t *status_ret)
{
    uint8_t       cleanup_rec[0x18];   /* (-0x18,A6)                    */
    status_$t     cleanup_status;      /* (-0x1C,A6)                    */
    int32_t       ec_value;            /* D2                            */
    uint16_t      widx;                /* D3: current record index      */
    uint16_t      first;               /* D4: where the walk started    */
    ec2_waiter_t *w;                   /* A3                            */

    /* 0x00E42868-0x00E4286C */
    *status_ret = status_$ok;

    /* 0x00E4286E-0x00E42884: arm the cleanup handler. */
    cleanup_status = FIM_$CLEANUP(cleanup_rec);
    if (cleanup_status != status_$cleanup_handler_set) {
        /* 0x00E4291A-0x00E42932: a fault unwound through here - release
         * the lock and re-signal. */
        ML_$UNLOCK(EC2_LOCK_ID);
        FIM_$SIGNAL(cleanup_status);
        return;
    }

    /* 0x00E42888-0x00E42894: ML_$LOCK(6). */
    ML_$LOCK(EC2_LOCK_ID);

    /* 0x00E42896-0x00E428A0: value and waiter word; no waiters is done. */
    ec_value = ec->value;
    widx     = (uint16_t)ec->awaiters;
    if (widx != 0) {
        /* 0x00E428A2-0x00E428B6: the head must be a live record (index
         * <= 0xE0, proc_id != 0, `tst.w (0x8,A5,D0w*0x1)`). */
        if (widx > 0xE0 || EC2_WAITER_TABLE_BASE[widx].proc_id == 0) {
            /* 0x00E428F6-0x00E428FA */
            *status_ret = status_$ec2_bad_event_count;
        } else {
            /* 0x00E428B8-0x00E428BA: remember the start of the ring. */
            first = widx;
            do {
                /* 0x00E428C0-0x00E428CE: w = &table[widx]; step to next. */
                w    = &EC2_WAITER_TABLE_BASE[widx];
                widx = (uint16_t)w->next;

                /* 0x00E428D2-0x00E428D6: reached its wait value? */
                if ((int32_t)(ec_value - w->wait_val) >= 0) {
                    /* 0x00E428D8-0x00E428EE: EC_$ADVANCE the waiter's
                     * per-process eventcount, `pea (-0xc,A2,D1w*0x1)` off
                     * 0xE20F6C with D1w = proc_id * 12. */
                    EC_$ADVANCE((ec_$eventcount_t *)
                                (EC2_$WAIT_ECS
                                 + (uint16_t)((uint16_t)w->proc_id * EC2_WAIT_EC_SIZE)
                                 - EC2_WAIT_EC_SIZE));
                }
                /* 0x00E428F0-0x00E428F2: until the ring comes round. */
            } while (widx != first);
        }
    }

    /* 0x00E42900-0x00E42912: ML_$UNLOCK(6), release the handler. */
    ML_$UNLOCK(EC2_LOCK_ID);
    FIM_$RLS_CLEANUP(cleanup_rec);

    /* 0x00E42932-0x00E4293A */
}
