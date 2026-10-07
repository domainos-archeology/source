/*
 * EC2_$REGISTER_EC1 - Register a level-1 eventcount for EC2 access
 *
 * Under ML lock 6 (ec2_$lock) first scans registration slots
 * 1 .. DAT_00e7cf06 for an entry already holding this EC1 and returns its
 * index if found; otherwise appends the EC1 at the next slot
 * (++_DAT_00e7cf04) unless the table is already at 0x100 entries, which is
 * reported as status_$ec2_internal_table_exhausted with a NULL result.
 * The index comes back in A0 (`movea.l A2,A0`, 0x00E429CE).
 *
 * Parameters:
 *   ec1        - EC1 pointer to register (argument 1, (0x8,A6))
 *   status_ret - Status return (argument 2, (0xC,A6)), passed by reference
 *
 * Returns:
 *   EC2 index for the registered EC1, or NULL when the table is full.
 *
 * Original address: 0x00e4293c (map: EC2, 0xE42358 size 0x908)
 * Re-emitted from the disassembly 0x00E4293C-0x00E429D8.
 */

#include "ec/ec_internal.h"
#include "ml/ml.h"

void *EC2_$REGISTER_EC1(ec_$eventcount_t *ec1, status_$t *status_ret)
{
    status_$t status;      /* (-0x4,A6)                          */
    int32_t   index;       /* (-0x8,A6): the index handed back   */
    int16_t   probe;       /* D0w: dbf counter                   */
    int16_t   slot;        /* D1w: current registration slot     */
    void     *result;      /* A2                                 */

    /* 0x00E4294E: status ok unless the table is full. */
    status = status_$ok;

    /* 0x00E42952-0x00E4295E: ML_$LOCK(6) (with the compiler's spare
     * result slot). */
    ML_$LOCK(EC2_LOCK_ID);

    /* 0x00E42960-0x00E42966: `move.w (0xe9a,A5),D0w / subq.w #1 / bmi` -
     * scan DAT_00e7cf06 slots starting at slot 1, if there are any. */
    probe = (int16_t)(DAT_00e7cf06 - 1);
    if (probe >= 0) {
        /* 0x00E42968-0x00E4296C: A0 = A5 + 4 (slot 1), D1 = 1. */
        slot = 1;
        do {
            /* 0x00E4296E-0x00E42976: `cmpa.l (0xa8c,A0),A1` */
            if ((ec_$eventcount_t *)DAT_00e7caf8[slot] == ec1) {
                /* 0x00E42978-0x00E42980: `ext.l D1` - return this slot. */
                index  = (int32_t)slot;
                result = ARCH_VA_TO_PTR((uint32_t)index);
                goto done;
            }
            /* 0x00E42982-0x00E42986 */
            slot++;
            probe--;
        } while (probe >= 0);
    }

    /* 0x00E4298A-0x00E42992: `cmpi.l #0x100,(0xe98,A5) / bcs` - UNSIGNED. */
    if (_DAT_00e7cf04 >= 0x100) {
        /* 0x00E42994-0x00E4299E */
        status = status_$ec2_internal_table_exhausted;          /* 0x180001 */
        result = NULL;
    } else {
        /* 0x00E429A0-0x00E429BA: bump the high-water mark and store the
         * EC1 at that slot (stride 4 off A5 + 0xA8C); the new index is
         * the result. */
        _DAT_00e7cf04++;
        DAT_00e7caf8[_DAT_00e7cf04] = ec1;
        index  = (int32_t)_DAT_00e7cf04;
        result = ARCH_VA_TO_PTR((uint32_t)index);
    }

done:
    /* 0x00E429BE-0x00E429CA: ML_$UNLOCK(6), publish the status. */
    ML_$UNLOCK(EC2_LOCK_ID);
    *status_ret = status;

    /* 0x00E429CE-0x00E429D8: result in A0 (and D0 for C callers). */
    ARCH_RESULT_A0(result);
    return result;
}
