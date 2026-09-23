/*
 * EC2_$INIT_S - System initialization for the EC2 subsystem
 *
 * Runs once at boot (called from 0x00E33D24):
 *   - EC_$INITs the 64 per-process wait eventcounts in EC2_$WAIT_ECS
 *     (0xE20F6C, stride 0x0C),
 *   - threads the 225 EC2 waiter records at 0xE7C06C onto a free list
 *     (record i's next = i + 1, wait_val and proc_id cleared),
 *   - resets the module cells: registration count, free-list head,
 *     highest registered index (1), registration slot 1 pointing at that
 *     count cell, and both PBU-pool bitmaps.
 *
 * Original address: 0x00e30970 (map: EC2, 0xE30970 size 0x84)
 * Re-emitted from the disassembly 0x00E30970-0x00E309F0.
 */

#include "ec/ec_internal.h"

void EC2_$INIT_S(void)
{
    int16_t       count;    /* D2w / D1w: dbf counters             */
    uint16_t      prev;     /* D2w in the waiter loop              */
    uint16_t      link;     /* D0w: prev + 1                       */
    uint8_t      *wait_ec;  /* A2: current EC2_$WAIT_ECS record + 0x0C */
    ec2_waiter_t *waiter;   /* A0: current waiter record           */

    /* 0x00E30978-0x00E30994: `movea.l #0xe20f6c,A0 / moveq #0x3f,D2 /
     * lea (0xc,A0),A2` then `pea (-0xc,A2) / jsr EC_$INIT / lea (0xc,A2),A2
     * / dbf D2w` - 64 eventcounts, 0x0C bytes apart. */
    wait_ec = EC2_$WAIT_ECS + EC2_WAIT_EC_SIZE;
    count   = 0x3F;
    do {
        EC_$INIT((ec_$eventcount_t *)(wait_ec - EC2_WAIT_EC_SIZE));
        wait_ec += EC2_WAIT_EC_SIZE;
        count--;
    } while (count >= 0);

    /* 0x00E30998-0x00E309B8: `move.w #0xe0,D1w / clr.w D2w /
     * movea.l #0xe7c06c,A0` then per record: `clr.w (0x8,A0) / clr.l (A0) /
     * D0w = D2w + 1 / move.w D0w,(0x4,A0) / D2w = D0w / lea (0xc,A0),A0 /
     * dbf D1w` - 0xE1 = 225 records. */
    count  = 0xE0;
    prev   = 0;
    waiter = EC2_WAITER_TABLE_BASE;
    do {
        waiter->proc_id  = 0;
        waiter->wait_val = 0;
        link             = (uint16_t)(prev + 1);
        waiter->next     = (int16_t)link;
        prev             = link;
        waiter++;
        count--;
    } while (count >= 0);

    /* 0x00E309BC-0x00E309E4: the module cells (A1 = A3 = 0xE7C06C). */
    DAT_00e7caf0  = 0;                  /* clr.w (0xa84,A1)        */
    DAT_00e7cf08  = 1;                  /* move.w #0x1,(0xe9c,A1)  */
    _DAT_00e7cf04 = 1;                  /* moveq #1 / move.l D0,(0xe98,A1) */
    DAT_00e7cafc  = &_DAT_00e7cf04;     /* lea (0xe98,A1),A1 / move.l A1,(0xa90,A3) */
    DAT_00e7cf00  = 0;                  /* clr.l (0xe94,A3)        */
    DAT_00e7cefc  = 0;                  /* clr.l (0xe90,A3)        */

    /* 0x00E309E8-0x00E309F0 */
}
