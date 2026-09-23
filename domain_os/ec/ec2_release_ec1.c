/*
 * EC2_$RELEASE_EC1 - Release a PBU-pool level-1 eventcount
 *
 * Under ML lock 6 (ec2_$lock) reads the EC2 index the caller points at.
 * It must be a PBU-pool index (0x101..0x120) whose record is marked in the
 * allocation bitmap.  If the record's waiter reference word (record + 0x0E)
 * is zero the allocation bit is cleared at once; otherwise every waiter is
 * released with EC_$ADVANCE_ALL and the record is marked in the
 * pending-release bitmap for EC2_$ALLOCATE_EC1 to reclaim once the
 * reference word drops to zero.
 *
 * Parameters:
 *   ec         - pointer to the EC2 index longword (argument 1, (0x8,A6))
 *   status_ret - Status return (argument 2, (0xC,A6)), passed by reference:
 *                status_$ok, status_$ec2_bad_event_count (0x180004) or
 *                status_$ec2_level_1_ec_not_allocated (0x180006)
 *
 * Original address: 0x00e42b32 (map: EC2, 0xE42358 size 0x908)
 * Re-emitted from the disassembly 0x00E42B32-0x00E42BDE.
 */

#include "ec/ec_internal.h"
#include "ml/ml.h"

void EC2_$RELEASE_EC1(ec2_$eventcount_t *ec, status_$t *status_ret)
{
    uint32_t  index;     /* D0                                  */
    uint16_t  pbu_slot;  /* D2w: index - 0x101                  */
    uint8_t  *rec;       /* A2: the EC2_$PBU_ECS record         */
    status_$t status;    /* (-0x4,A6)                           */

    /* 0x00E42B40-0x00E42B4C: ML_$LOCK(6) (with the compiler's spare
     * result slot). */
    ML_$LOCK(EC2_LOCK_ID);

    /* 0x00E42B4E-0x00E42B62: unsigned 0x101 <= index <= 0x120. */
    index = (uint32_t)ec->value;
    if (index < 0x101 || index > 0x120) {
        /* 0x00E42B64-0x00E42B6C */
        status = status_$ec2_bad_event_count;
    } else {
        /* 0x00E42B6E-0x00E42B7A: word subtract, `btst.l D2,D1` on the
         * allocation bitmap. */
        pbu_slot = (uint16_t)(index - 0x101);
        if ((DAT_00e7cf00 & ((uint32_t)1 << (pbu_slot & 0x1F))) == 0) {
            /* 0x00E42B7C-0x00E42B84 */
            status = status_$ec2_level_1_ec_not_allocated;
        } else {
            /* 0x00E42B86-0x00E42B9A: record = 0xE88460 + slot * 0x18 (word
             * displacement); status ok. */
            rec    = EC2_$PBU_ECS + (uint16_t)(pbu_slot * EC2_PBU_EC_SIZE);
            status = status_$ok;

            /* 0x00E42B9E-0x00E42BA2: `tst.w (0xe,A2)` - any waiters? */
            if (*(int16_t *)(rec + 0x0E) == 0) {
                /* 0x00E42BA4-0x00E42BAE: free the slot now. */
                DAT_00e7cf00 &= ~((uint32_t)1 << (pbu_slot & 0x1F));
            } else {
                /* 0x00E42BB0-0x00E42BBE: EC_$ADVANCE_ALL(record), then
                 * mark it pending release. */
                EC_$ADVANCE_ALL((ec_$eventcount_t *)rec);
                DAT_00e7cefc |= ((uint32_t)1 << (pbu_slot & 0x1F));
            }
        }
    }

    /* 0x00E42BC2-0x00E42BD2: ML_$UNLOCK(6), publish the status. */
    ML_$UNLOCK(EC2_LOCK_ID);
    *status_ret = status;

    /* 0x00E42BD6-0x00E42BDE */
}
