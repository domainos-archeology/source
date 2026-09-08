/*
 * DIR_$CLEANUP - release every directory handle the current process holds
 *
 * Original address: 0x00E53578
 * Original size:    432 bytes (0x00E53578..0x00E53728)
 * SAU2 map:         "I E4AF28 DIR size = 8FD0"; the entry point is exported
 *                   as DIR_$CLEANUP.
 *
 * Called from NAME_$CLEANUP (0x00E4FEE6) and DIR_$SERVER (0x00E58612) when a
 * process is torn down.  It walks the 32 dir_$handle_t slots at A5+0x1880,
 * and for every slot the in-use bitmap at A5+0x203C marks and whose owner
 * word matches PROC1_$CURRENT it:
 *
 *   - if the handle is in the middle of a page split (+0x0E true) and a page
 *     is still wired (+0x1C != 2), scans the directory backwards from its
 *     last page for the page whose header page number matches the wired
 *     buffer's, copies the mapped page over that buffer, invalidates the
 *     buffer and unwires it;
 *   - runs dir_$validate_pages with crash_flag = true;
 *   - releases the handle with dir_$release_handle.
 *
 * When the loop is done it drops DIR_$LINK_BUF_MUTEX if this process owns it
 * and calls DIR_$OLD_CLEANUP.
 */

#include "dir/dir_internal.h"

void DIR_$CLEANUP(void)
{
    /*
     * 0x00E53578-0x00E535A0: link.w A6,-0x30 / lea (0xe7dc00).l,A5.
     *
     * D2 is the `moveq #0x1f,D2` + `dbf` counter (32 slots), D3 the slot
     * number, and the frame cells (-0x2c,A6) and (-0x30,A6) are two copies of
     * the same A5-based cursor that the image advances by 0x3C per slot; they
     * are written here as an index into DIR_$HANDLE_TAB.
     *
     * D5 and D6 are loaded with 0xE1737C (&UID_$NIL) and 0xE20608
     * (&PROC1_$CURRENT) at 0x00E5358A-0x00E53596 and then never used - both
     * references below are absolute in the image.
     */
    int16_t   count;                 /* D2 */
    uint16_t  slot;                  /* D3 */
    status_$t status;                /* (-0x18,A6) */

    slot  = 0;
    count = 0x1F;

    do {
        /*
         * 0x00E535A2-0x00E535BA: the slot must be marked in use and owned by
         * this process.  `btst.l D3,D0` on the longword bitmap, then
         * `cmp.w (0x00e20608).l,D1w` against PROC1_$CURRENT.
         */
        if ((DIR_$HANDLE_IN_USE & (1u << (slot & 0x1F))) != 0 &&
            DIR_$HANDLE_TAB[slot].owner == (int16_t)PROC1_$CURRENT) {

            /* 0x00E535BE-0x00E535C6: A1 = &handle, cached in (-0x1c,A6). */
            dir_$handle_t *h = &DIR_$HANDLE_TAB[slot];

            /* 0x00E535CA: tst.b (0xe,A1) / bpl - Domain boolean. */
            if (h->split_busy < 0) {
                /*
                 * 0x00E535D2: a max_slots of 2 means dir_$release_wire has
                 * already run, so there is no wired page to put back.
                 */
                if (h->max_slots != 2) {
                    /*
                     * 0x00E535DC-0x00E53686: scan the directory backwards
                     * from its last page looking for the page the wired
                     * buffer belongs to.
                     */
                    int16_t   page;      /* D4 */
                    uid_t     dir_uid;   /* (-0x8,A6) .. (-0x1,A6) */
                    dir_page_hdr_t *hdr; /* A2, from dir_$map_page's A0 */
                    uint16_t  kind;
                    uint16_t  version;

                    /* (length >> 10) - 1: lsr.l #8 / lsr.l #2 / subq.l #1. */
                    page = (int16_t)((h->length >> 10) - 1);

                    /* 0x00E535EC: dir_uid := UID_$NIL. */
                    dir_uid.high = UID_$NIL.high;
                    dir_uid.low  = UID_$NIL.low;

                    for (;;) {
                        /*
                         * 0x00E535F4-0x00E53606: subq.l #2,SP is the Pascal
                         * function-result slot; the result comes back in A0.
                         */
                        hdr = (dir_page_hdr_t *)dir_$map_page(h, page);

                        /*
                         * 0x00E53608-0x00E53626: the first page visited fixes
                         * the directory UID every later page must carry.
                         */
                        if (dir_uid.high == UID_$NIL.high &&
                            dir_uid.low  == UID_$NIL.low) {
                            dir_uid.high = hdr->dir_uid_high;
                            dir_uid.low  = hdr->dir_uid_low;
                        }

                        /* 0x00E53628-0x00E53630: (page[0] & 0xC0) >> 6. */
                        kind = (uint16_t)((hdr->kind & DIR_PAGE_KIND_MASK)
                                          >> DIR_PAGE_KIND_SHIFT);
                        /* 0x00E53638-0x00E5363E: page[1] & 0x3F. */
                        version = (uint16_t)(hdr->version & DIR_PAGE_VERSION_MASK);

                        /*
                         * 0x00E53630-0x00E53658.  The image crashes unless
                         * the header is well formed AND its page number
                         * differs from the index it was read at: a page that
                         * is already where it says it belongs means there is
                         * nothing left half-split, which cannot happen while
                         * max_slots says a page is still wired.  The same
                         * equality ends the scan (rather than crashing) in
                         * dir_$validate_pages at 0x00E537B4.
                         */
                        if ((kind != 0 && kind != 1) ||
                            version == 0 ||
                            dir_uid.high != hdr->dir_uid_high ||
                            dir_uid.low  != hdr->dir_uid_low ||
                            (uint16_t)page == hdr->page_no) {
                            CRASH_SYSTEM(DIR_$CRASH_STATUS);   /* 0x00E5365A */
                        }

                        /* 0x00E53666-0x00E53672 */
                        if (hdr->page_no ==
                            ((dir_page_hdr_t *)ARCH_VA_TO_PTR(h->buf))->page_no) {
                            break;
                        }

                        /* 0x00E53674-0x00E53682: ran off the front. */
                        if (page == 0) {
                            CRASH_SYSTEM(DIR_$CRASH_STATUS);
                        }

                        page--;                 /* 0x00E53684 */
                    }

                    /*
                     * 0x00E5368A-0x00E5369A: copy the mapped page over the
                     * handle's wired buffer.  `move.w #0xff,D0` + `dbf` is
                     * 0x100 longwords = 0x400 bytes = one directory page.
                     */
                    {
                        uint32_t       *dst = (uint32_t *)ARCH_VA_TO_PTR(h->buf);
                        const uint32_t *src = (const uint32_t *)hdr;
                        int16_t         i   = 0xFF;

                        do {
                            *dst++ = *src++;
                            i--;
                        } while (i != -1);
                    }

                    /*
                     * 0x00E5369E-0x00E536AA: invalidate the buffer - clear
                     * the longword at +0x02 (the directory UID's high half)
                     * and bit 4 of the flags byte.
                     */
                    ((dir_page_hdr_t *)ARCH_VA_TO_PTR(h->buf))->dir_uid_high = 0;
                    ((dir_page_hdr_t *)ARCH_VA_TO_PTR(h->buf))->kind &= (uint8_t)~0x10;

                    /* 0x00E536AE-0x00E536B6 */
                    dir_$release_wire(h);
                }

                /*
                 * 0x00E536B8-0x00E536C6.  subq.l #2,SP is the Pascal result
                 * slot, `st -(SP)` pushes the boolean true that makes
                 * dir_$validate_pages crash rather than report an internal
                 * error, and `lea (0xc,SP),SP` pops 0x0C bytes.
                 */
                dir_$validate_pages(h, (char)0xFF, &status);
            } else {
                /*
                 * 0x00E536CC-0x00E536DE: with no split in progress the
                 * handle must not have a page wired.
                 */
                if (h->max_slots != 2) {
                    CRASH_SYSTEM(DIR_$CRASH_STATUS);
                }
            }

            /*
             * 0x00E536E0-0x00E536E8: pea (-0x1c,A6) - dir_$release_handle
             * takes the address of the handle pointer so it can clear it.
             */
            {
                dir_$handle_t *hp = h;
                dir_$release_handle(&hp);
            }
        }

        /* 0x00E536EA-0x00E536F6: moveq #0x3c,D0 / addq.w #1,D3 / dbf D2. */
        slot++;
        count--;
    } while (count != -1);

    /*
     * 0x00E536FA-0x00E53716: give up DIR_$LINK_BUF_MUTEX if this process is
     * still recorded as its owner.  dir_$do_op_cname ends the same way at
     * 0x00E51B14-0x00E51B22 (`cmp.w (0x2040,A5)` / `clr.w (0x2040,A5)` /
     * ML_$EXCLUSION_STOP with 0xE2C1D8).
     */
    if ((int16_t)PROC1_$CURRENT == DIR_$LINK_BUF_OWNER) {
        DIR_$LINK_BUF_OWNER = 0;
        ML_$EXCLUSION_STOP(&DIR_$LINK_BUF_MUTEX);
    }

    /* 0x00E53718 */
    DIR_$OLD_CLEANUP();
}
