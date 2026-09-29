/*
 * DIR_$INIT - build the DIR module's lock and handle tables at boot
 *
 * Original address: 0x00E3140C
 * Original size:    232 bytes (0x00E3140C..0x00E314F4)
 * SAU2 map:         "I E3140C DIR size = E8" in the boot-time init segment,
 *                   exported as DIR_$INIT.
 *
 * The routine's own A5 is 0x00E3503C (`lea (0xe3503c).l,A5` at 0x00E31414,
 * the map's "D E3503C OLD_DIR size = 4"); it is loaded and never used.  The
 * DIR module block is reached absolutely with `movea.l #0xe7dc00,A0` at
 * 0x00E3141A - the block DIR_$DATA, as everywhere else.
 *
 * It clears the two in-use bitmaps, then in one 32-iteration loop numbers and
 * chains both tables - dir_$lock_entry_t[32] at A5+0x1680 and
 * dir_$handle_t[32] at A5+0x1880 - and initialises each slot's event counter.
 * Afterwards it breaks the three chain ends the free lists must not follow,
 * publishes the two list heads, initialises the two mutexes and the
 * wait-for-handle event counter, and calls DIR_$OLD_INIT.
 *
 * Note the image never touches A5+0x2040 (DIR_$DATA.link_buf_owner); it is zero in
 * the image and only DIR_$CLEANUP and dir_$do_op_cname write it.
 */

#include "dir/dir_internal.h"

void DIR_$INIT(void)
{

    int16_t  count;                 /* D2: `moveq #0x1f,D2` + dbf        */
    uint16_t i;                     /* D3: slot number                   */

    dir_$lock_entry_t *lock_tab   = DIR_$DATA.lock_tab;
    dir_$handle_t     *handle_tab = DIR_$DATA.handle_tab;

    /* 0x00E31420 / 0x00E31424: both bitmaps are cleared as longwords. */
    DIR_$DATA.handle_in_use = 0;
    DIR_$DATA.lock_in_use   = 0;

    /*
     * 0x00E3144C-0x00E31494.  The image keeps three cursors that step by
     * 0x3C (D5, D6 and (-0x8,A6)), two that step by 0x10 (A2, A3/A4) and one
     * that steps by 0xC (D4, the event-counter array); they are written here
     * as the single index D3 carries.
     */
    i     = 0;
    count = 0x1F;
    do {
        /* 0x00E3144C: EC_$INIT(&DIR_$WAIT_ECS[i]), stride 0xC from 0xE2C058. */
        EC_$INIT(&DIR_$WAIT_ECS[i]);

        /* 0x00E31458: move.w D3w,(0x18b8,A0) - handle slot number. */
        handle_tab[i].slot_index = i;

        /* 0x00E3145E/0x00E31466: lea (0x18bc,A1),A0 / move.l A0,(0x18b0,A1). */
        handle_tab[i].next = ARCH_PTR_TO_VA(&handle_tab[i + 1]);

        /* 0x00E3146A/0x00E3146E: lea (0x1690,A4),A0 / move.l A0,(0x1680,A3). */
        lock_tab[i].u.next = ARCH_PTR_TO_VA(&lock_tab[i + 1]);

        /* 0x00E31472: move.w D3w,(0x168e,A2) - lock entry slot number. */
        lock_tab[i].index = i;

        i++;                        /* 0x00E31476 */
        count--;
    } while (count != -1);          /* 0x00E31494 */

    /*
     * 0x00E3149E-0x00E314A6.  The loop ran one link past each table, and
     * handle slot 0 is the emergency handle DIR_$ALLOC_HANDLE hands out
     * directly (0x00E4B8DA) rather than taking from the free list, so it is
     * unchained too.  The image clears them in this order.
     */
    lock_tab[DIR_SLOT_COUNT - 1].u.next = 0;        /* clr.l (0x1870,A0) */
    handle_tab[DIR_SLOT_COUNT - 1].next = 0;        /* clr.l (0x1ff4,A0) */
    handle_tab[0].next                  = 0;        /* clr.l (0x18b0,A0) */

    /* 0x00E314AA/0x00E314B2: the two free-list heads. */
    DIR_$DATA.lock_free   = ARCH_PTR_TO_VA(&lock_tab[0]);   /* 0x00E7F280 */
    DIR_$DATA.handle_free = ARCH_PTR_TO_VA(&handle_tab[1]); /* 0x00E7F4BC */

    /* 0x00E314BA: 0xE2C1F0 */
    ML_$EXCLUSION_INIT(&DIR_$MUTEX);
    /* 0x00E314C8: 0xE2C1D8 */
    ML_$EXCLUSION_INIT(&DIR_$LINK_BUF_MUTEX);
    /* 0x00E314D6: 0xE2C048 */
    EC_$INIT(&DIR_$WT_FOR_HDNL_EC);

    /* 0x00E314E4 */
    DIR_$OLD_INIT();
}
