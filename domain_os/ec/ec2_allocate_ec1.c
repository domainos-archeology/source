/*
 * EC2_$ALLOCATE_EC1 - Allocate a level-1 eventcount from the PBU pool
 *
 * Scans the 32 records of EC2_$PBU_ECS under ML lock 6 (ec2_$lock) for a
 * record that is either
 *   - marked in the pending-release bitmap (DAT_00e7cefc) with a zero
 *     waiter reference word at record + 0x0E, or
 *   - not marked in the allocation bitmap (DAT_00e7cf00),
 * marks it allocated, EC_$INITs it, clears its reference word and returns
 * its EC2 index, 0x101 + slot, in A0 (`movea.l A4,A0`, 0x00E42A7E).
 *
 * Parameters:
 *   status_ret - Status return (argument 1, (0x8,A6)), passed by reference:
 *                status_$ok on success,
 *                status_$ec2_unable_to_allocate_level_1_eventcount (0x180005)
 *                when all 32 records are in use
 *
 * Returns:
 *   EC2 index 0x101..0x120 for the allocated EC1, or 0 (`suba.l A4,A4`,
 *   0x00E42A6C) when none is free.
 *
 * Original address: 0x00e429da (map: EC2, 0xE42358 size 0x908)
 * Re-emitted from the disassembly 0x00E429DA-0x00E42A88.
 */

#include "ec/ec_internal.h"
#include "ml/ml.h"

void *EC2_$ALLOCATE_EC1(status_$t *status_ret)
{
    int16_t   probe;     /* D0w: dbf counter                          */
    uint16_t  slot;      /* D1w: record index 0..0x1F                 */
    uint8_t  *rec;       /* A0/A2: current EC2_$PBU_ECS record        */
    uint32_t  bit;       /* D0: 1 << slot                             */
    void     *result;    /* A4                                        */

    /* 0x00E429EE-0x00E429F8: ML_$LOCK(6).  (The `subq.l #0x2,SP` at
     * 0x00E429E8 is a Pascal result slot the compiler emits for the
     * lock call; ML_$LOCK returns nothing.) */
    ML_$LOCK(EC2_LOCK_ID);

    /* 0x00E429FA-0x00E42A04: `moveq #0x1f,D0 / clr.w D1w /
     * movea.l #0xe88460,A0` - 32 records, 0x18 bytes apart. */
    probe = 0x1F;
    slot  = 0;
    rec   = EC2_$PBU_ECS;

    do {
        /* 0x00E42A06-0x00E42A0C: is this record pending release? */
        if ((DAT_00e7cefc & ((uint32_t)1 << slot)) != 0) {
            /* 0x00E42A0E-0x00E42A12: `tst.w (0xe,A2)` - waiters gone? */
            if (*(int16_t *)(rec + 0x0E) == 0) {
                /* 0x00E42A14-0x00E42A24: mark it allocated, drop the
                 * pending-release mark, and join the found path. */
                bit = (uint32_t)1 << slot;
                DAT_00e7cf00 |= bit;
                DAT_00e7cefc &= ~bit;
                goto found;
            }
        }

        /* 0x00E42A26-0x00E42A2C: `btst.l D1,D3` on the allocation bitmap -
         * an unmarked record is free. */
        if ((DAT_00e7cf00 & ((uint32_t)1 << slot)) == 0) {
            goto found;
        }

        /* 0x00E42A60-0x00E42A66: next record. */
        slot++;
        rec += EC2_PBU_EC_SIZE;
        probe--;
    } while (probe >= 0);

    /* 0x00E42A6A-0x00E42A78: nothing free - unlock and report. */
    result = NULL;
    ML_$UNLOCK(EC2_LOCK_ID);
    *status_ret = status_$ec2_unable_to_allocate_level_1_eventcount;
    return result;

found:
    /* 0x00E42A2E-0x00E42A32: the allocation bit is set (again) here -
     * the pending-release path above already set it; the image ORs it
     * twice and so do we. */
    bit = (uint32_t)1 << slot;
    DAT_00e7cf00 |= bit;

    /* 0x00E42A36-0x00E42A40: result = slot + 0x101 (slot zero-extended
     * from the word). */
    result = ARCH_VA_TO_PTR((uint32_t)slot + 0x101);

    /* 0x00E42A42-0x00E42A4C: EC_$INIT(record) then clear the waiter
     * reference word at record + 0x0E. */
    EC_$INIT((ec_$eventcount_t *)rec);
    *(int16_t *)(rec + 0x0E) = 0;

    /* 0x00E42A50-0x00E42A5C: ML_$UNLOCK(6); status ok. */
    ML_$UNLOCK(EC2_LOCK_ID);
    *status_ret = status_$ok;

    /* 0x00E42A7E-0x00E42A88: result in A0 (and D0 for C callers). */
    ARCH_RESULT_A0(result);
    return result;
}
