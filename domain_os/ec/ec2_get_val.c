/*
 * EC2_$GET_VAL - Read the value behind an EC2 index
 *
 * Translates the EC2 index the caller points at to its level-1 eventcount
 * and returns that eventcount's value.  No lock is taken.
 *
 * Index ranges (0x00E42BF8-0x00E42C1E):
 *   1 .. _DAT_00e7cf04   registered EC1s (DAT_00e7caf8[index]; index 1 is
 *                        the registration-count cell EC2_$INIT_S planted)
 *   0x101 .. 0x120       PBU pool records, if marked allocated
 *   0, everything else   status_$ec2_bad_event_count
 *
 * Parameters:
 *   ec         - pointer to the EC2 index longword (argument 1, (0x8,A6))
 *   status_ret - Status return (argument 2, (0xC,A6)), passed by reference
 *
 * Returns:
 *   The level-1 eventcount's value, or 0x7FFFFFFF on any error.
 *
 * Original address: 0x00e42be0 (map: EC2, 0xE42358 size 0x908)
 * Re-emitted from the disassembly 0x00E42BE0-0x00E42C5E.
 */

#include "ec/ec_internal.h"

int32_t EC2_$GET_VAL(ec2_$eventcount_t *ec, status_$t *status_ret)
{
    uint32_t          index;     /* D0                          */
    uint16_t          pbu_slot;  /* D0w after `subi.w #0x101`   */
    ec_$eventcount_t *ec1;       /* A3                          */
    int32_t           value;     /* D0 at exit                  */

    /* 0x00E42BF6: status cleared up front. */
    *status_ret = status_$ok;

    /* 0x00E42BF8-0x00E42C00: `move.l (A1),D0 / beq` then
     * `cmp.l (0xe98,A5),D0 / bhi` - unsigned 1 <= index <= max registered. */
    index = (uint32_t)ec->value;
    if (index != 0 && index <= _DAT_00e7cf04) {
        /* 0x00E42C02-0x00E42C0E: `lsl.w #2,D0w` - a WORD displacement of
         * index * 4 into the registration table at A5 + 0xA8C, then the
         * eventcount's value longword. */
        ec1   = (ec_$eventcount_t *)DAT_00e7caf8[(uint16_t)(index << 2) >> 2];
        value = ec1->value;
    } else if (index < 0x101 || index > 0x120) {
        /* 0x00E42C10-0x00E42C1E, 0x00E42C4A-0x00E42C50 */
        *status_ret = status_$ec2_bad_event_count;             /* 0x180004 */
        value = 0x7FFFFFFF;
    } else {
        /* 0x00E42C20-0x00E42C2A: word subtract, `btst.l D0,D1` on the
         * allocation bitmap. */
        pbu_slot = (uint16_t)(index - 0x101);
        if ((DAT_00e7cf00 & ((uint32_t)1 << (pbu_slot & 0x1F))) != 0) {
            /* 0x00E42C2C-0x00E42C3C: slot * 0x18 as a word displacement
             * off 0xE88460; the value longword is the record's first
             * field. */
            value = ((ec_$eventcount_t *)
                     (EC2_$PBU_ECS + (uint16_t)(pbu_slot * EC2_PBU_EC_SIZE)))
                    ->value;
        } else {
            /* 0x00E42C42-0x00E42C50 */
            *status_ret = status_$ec2_level_1_ec_not_allocated; /* 0x180006 */
            value = 0x7FFFFFFF;
        }
    }

    /* 0x00E42C56-0x00E42C5E */
    return value;
}
