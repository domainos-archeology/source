/*
 * EC2_$GET_EC1_ADDR - Resolve an EC2 index to the level-1 eventcount address
 *
 * Under ML lock 6 (ec2_$lock) reads the longword the caller points at and
 * maps it:
 *   2 .. _DAT_00e7cf04      -> registered EC1 pointer DAT_00e7caf8[index]
 *   0x101 .. 0x120          -> EC2_$PBU_ECS record (index - 0x101) * 0x18,
 *                              if that record is marked allocated
 *   anything else           -> status_$ec2_bad_event_count
 * The result comes back in A0 (`movea.l A2,A0`, 0x00E42B26) and is NULL
 * (`suba.l A2,A2`, 0x00E42AAA) on every failure path.
 *
 * Parameters:
 *   ec         - pointer to the EC2 index longword (argument 1, (0x8,A6))
 *   status_ret - Status return (argument 2, (0xC,A6)), passed by reference
 *
 * Returns:
 *   Pointer to the level-1 eventcount, or NULL.
 *
 * Original address: 0x00e42a8a (map: EC2, 0xE42358 size 0x908)
 * Re-emitted from the disassembly 0x00E42A8A-0x00E42B30.
 */

#include "ec/ec_internal.h"
#include "ml/ml.h"

ec_$eventcount_t *EC2_$GET_EC1_ADDR(ec2_$eventcount_t *ec, status_$t *status_ret)
{
    uint32_t          index;     /* D0                              */
    uint16_t          pbu_slot;  /* D0w after `subi.w #0x101`       */
    ec_$eventcount_t *result;    /* A2                              */
    status_$t         status;    /* (-0x4,A6)                       */

    /* 0x00E42A98-0x00E42AA4: ML_$LOCK(6) (with the compiler's spare
     * result slot). */
    ML_$LOCK(EC2_LOCK_ID);

    /* 0x00E42AA6-0x00E42AAC: NULL result, fetch the index longword. */
    result = NULL;
    index  = (uint32_t)ec->value;

    /* 0x00E42AAE-0x00E42ABA: `cmpi.l #0x1,D0 / bls` and
     * `cmp.l (0xe98,A5),D0 / bhi` - unsigned 2 <= index <= max registered. */
    if (index > 1 && index <= _DAT_00e7cf04) {
        /* 0x00E42ABC-0x00E42AC8: registration table, stride 4. */
        result = (ec_$eventcount_t *)DAT_00e7caf8[index];
        /* 0x00E42B0E */
        status = status_$ok;
    } else if (index < 0x101 || index > 0x120) {
        /* 0x00E42ACA-0x00E42AE2: outside the PBU range too. */
        status = status_$ec2_bad_event_count;                  /* 0x180004 */
    } else {
        /* 0x00E42AE4-0x00E42AEE: word subtract, then `btst.l D0,D1` on
         * the allocation bitmap. */
        pbu_slot = (uint16_t)(index - 0x101);
        if ((DAT_00e7cf00 & ((uint32_t)1 << (pbu_slot & 0x1F))) == 0) {
            /* 0x00E42AF0-0x00E42AF8 */
            status = status_$ec2_level_1_ec_not_allocated;     /* 0x180006 */
        } else {
            /* 0x00E42AFA-0x00E42B0A: `lsl.w #3` then + 2x = slot * 0x18,
             * as a word displacement off 0xE88460. */
            result = (ec_$eventcount_t *)
                     (EC2_$PBU_ECS + (uint16_t)(pbu_slot * EC2_PBU_EC_SIZE));
            /* 0x00E42B0E */
            status = status_$ok;
        }
    }

    /* 0x00E42B12-0x00E42B22: ML_$UNLOCK(6), then publish the status. */
    ML_$UNLOCK(EC2_LOCK_ID);
    *status_ret = status;

    /* 0x00E42B26-0x00E42B30: result in A0. */
    return result;
}
