/*
 * NETBUF_$ADD_PAGES - Grow the header and data buffer pools
 *
 * Clamps the two requested counts against the pool ceilings, wires that many
 * physical pages in one WP_$CALLOC_LIST call, then turns the first hdr_count
 * of them into 1KB header buffers on the header free list and threads the
 * remainder onto the data free list.  If the data pool ends up over its limit
 * the excess is handed straight back to NETBUF_$DEL_PAGES.
 *
 * Two word arguments (0x00E0E936 "move.w (0x8,A6),D5w" and 0x00E0E93A
 * "move.w (0xa,A6),D2w"); nothing is returned.
 *
 * Original address: 0x00E0E928 (510 bytes)
 * Module base: A5 = 0x00E245A8 (0x00E0E930 "lea (0xe245a8).l,A5")
 */

#include "netbuf/netbuf_internal.h"

void NETBUF_$ADD_PAGES(int16_t hdr_count, int16_t dat_count)
{
    uint32_t pages[NETBUF_MAX_ALLOC];   /* A6-0x210, 128 longwords */
    status_$t status;                   /* A6-0x214 */
    uint32_t va;                        /* A6-0x218 */
    ml_$spin_token_t token;             /* A6-0x21A */
    uint32_t zero16[4];                 /* A6-0x10, cleared at 0x00E0E9C4 */
    int16_t hdr_take;                   /* D3 */
    int16_t dat_take;                   /* D4 */
    int16_t total;                      /* D2, reused */
    int16_t i;

    token = ML_$SPIN_LOCK(&NETBUF_$SPIN_LOCK);      /* 0x00E0E942 */

    /*
     * 0x00E0E94E  cmpi.w #0xb0,(0x334,A5) / blt
     * 0x00E0E956  clr.w D3w
     * 0x00E0E95A - 0x00E0E970: otherwise clamp to 0xB0 - hdr_alloc, computed
     * in longs after "ext.l" on both operands.
     */
    if (NETBUF_$HDR_ALLOC >= NETBUF_HDR_MAX) {
        hdr_take = 0;
    } else {
        hdr_take = hdr_count;
        if ((int32_t)hdr_take > (int32_t)NETBUF_HDR_MAX - (int32_t)NETBUF_$HDR_ALLOC) {
            hdr_take = (int16_t)((int32_t)NETBUF_HDR_MAX - (int32_t)NETBUF_$HDR_ALLOC);
        }
    }

    /* 0x00E0E972 - 0x00E0E982: clamp to dat_lim - dat_cnt, again in longs. */
    dat_take = dat_count;
    if ((int32_t)dat_take > (int32_t)(NETBUF_$DAT_LIM - NETBUF_$DAT_CNT)) {
        dat_take = (int16_t)(NETBUF_$DAT_LIM - NETBUF_$DAT_CNT);
    }

    /*
     * 0x00E0E984  move.w D3w,D2w / add.w D4w,D2w
     * 0x00E0E988  cmpi.w #0x80,D2w / ble
     * 0x00E0E98E  pea (-0x6c,PC)   -> 0x00E0E924, the longword 0x00110001
     */
    total = (int16_t)(hdr_take + dat_take);
    if (total > NETBUF_MAX_ALLOC) {
        CRASH_SYSTEM(&netbuf_err);
    }

    /*
     * 0x00E0E99A "add.w D5w,(0x334,A5)" - the count charged to hdr_alloc is
     * the REQUESTED number, not the clamped one.
     */
    NETBUF_$HDR_ALLOC = (int16_t)(NETBUF_$HDR_ALLOC + hdr_count);

    ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);     /* 0x00E0E9A8 */

    /* 0x00E0E9B0 "tst.w D2w / ble" - a signed test on the clamped total. */
    if (total > 0) {
        WP_$CALLOC_LIST(total, pages);              /* 0x00E0E9BC */
    }

    /*
     * 0x00E0E9C4 - 0x00E0E9D2: "moveq #0x3 / lea (0x4,A6),A0 / clr.l (-0x14,A0)
     * / addq.l #0x4,A0 / dbf" zeroes the four longwords at A6-0x10.  They are
     * the source of the 16-byte block copy that clears each new header
     * buffer's 0x3EC..0x3FB.
     */
    zero16[0] = 0;
    zero16[1] = 0;
    zero16[2] = 0;
    zero16[3] = 0;

    /*
     * 0x00E0E9D6 - 0x00E0EA68: the header buffers.  "move.w D3w,D0w /
     * subq.w #0x1,D0w / bmi" skips the loop when hdr_take is 0.
     */
    for (i = 0; i < hdr_take; i++) {
        /* 0x00E0E9F0 - 0x00E0E9FA: NETBUF_$GETVA(pages[i] << 10, &va, &status) */
        NETBUF_$GETVA(pages[i] << 10, &va, &status);

        if (status != status_$ok) {                 /* 0x00E0EA06 */
            CRASH_SYSTEM(&status);                  /* 0x00E0EA10 */
        }

        /* 0x00E0EA1A "clr.l (0x3e8,A4)" */
        NETBUF_HDR_FIELD(va, NETBUF_HDR_DATA_OFF) = 0;

        /* 0x00E0EA1E - 0x00E0EA26: the page's physical address. */
        NETBUF_HDR_PHYS(va) = pages[i] << 10;

        /*
         * 0x00E0EA2A - 0x00E0EA38: four "move.l (A0)+,(A1)+" from the zeroed
         * block at A6-0x10 into 0x3EC..0x3FB.
         */
        NETBUF_HDR_FIELD(va, 0x3EC) = zero16[0];
        NETBUF_HDR_FIELD(va, 0x3F0) = zero16[1];
        NETBUF_HDR_FIELD(va, 0x3F4) = zero16[2];
        NETBUF_HDR_FIELD(va, 0x3F8) = zero16[3];

        /* 0x00E0EA3A - 0x00E0EA5C: push onto the header free list. */
        token = ML_$SPIN_LOCK(&NETBUF_$SPIN_LOCK);
        NETBUF_HDR_NEXT(va) = NETBUF_$HDR_TOP;
        NETBUF_$HDR_TOP = va;
        ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);
    }

    /* 0x00E0EA6C "tst.w D4w / ble" */
    if (dat_take > 0) {
        /*
         * 0x00E0EA72 - 0x00E0EAAC: chain pages[hdr_take] .. pages[total-1]
         * forward.  The loop runs with n = hdr_take+1 .. total-1 and does
         *   MMAPE[pages[n-1]].link = low word of pages[n]
         * because the source is (-0x20e,A2) (element n, low half) while the
         * MMAPE index comes from (-0x214,A1) (element n-1).
         */
        for (i = (int16_t)(hdr_take + 1); i <= (int16_t)(total - 1); i++) {
            NETBUF_DAT_NEXT(pages[i - 1]) = (uint16_t)pages[i];
        }

        token = ML_$SPIN_LOCK(&NETBUF_$SPIN_LOCK);  /* 0x00E0EAB4 */

        /*
         * 0x00E0EADA "move.w (0x326,A5),(-0x1ffa,A1)" - the old list head is
         * read as the LOW word of the dat_top longword and appended to the
         * last new page, before ...
         */
        NETBUF_DAT_NEXT(pages[total - 1]) = (uint16_t)NETBUF_$DAT_TOP;

        /* ... 0x00E0EAE8 replaces the head with the first new page. */
        NETBUF_$DAT_TOP = pages[hdr_take];

        /* 0x00E0EAF2 "add.l D1,(0x320,A5)" */
        NETBUF_$DAT_CNT += (uint32_t)(int32_t)dat_take;

        ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token); /* 0x00E0EAFE */
    }

    /*
     * 0x00E0EB06  move.l (0x320,A5),D0 / cmp.l (0x31c,A5),D0 / bls  (unsigned)
     * 0x00E0EB10  move.w (0x31e,A5),D1w / sub.w D0w,D1w  - a WORD subtraction
     *             of the two counters' low halves, so the count handed to
     *             NETBUF_$DEL_PAGES is negative.
     * 0x00E0EB18  clr.w -(SP)  - no header pages are removed.
     */
    if (NETBUF_$DAT_CNT > NETBUF_$DAT_LIM) {
        NETBUF_$DEL_PAGES(0, (int16_t)((uint16_t)NETBUF_$DAT_LIM -
                                       (uint16_t)NETBUF_$DAT_CNT));
    }
}
