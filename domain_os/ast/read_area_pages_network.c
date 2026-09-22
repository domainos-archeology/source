/*
 * ast_$read_area_pages_network - Read a run of a remote segment's pages
 *                                from the home node
 *
 * Allocates `count` frames (PMAP lock held on entry, released after),
 * hands their data buffers to NETBUF, and asks the home node for the run
 * with NETWORK_$READ_AHEAD (page size from the object's block-size
 * nibble, read-ahead suppressed when the AOTE was TOUCHED).  Frames the
 * read did not fill are taken back from NETBUF and freed.  When pages
 * arrived: if the first frame came back as zero the pages are "null" -
 * their frames are fetched from NETBUF, zeroed, and the entries get bit
 * 22; every read entry is reset to 1.  The DTU/DTM/DTA clocks are taken
 * from the reply (or the local clock when the reply's DTM is zero), the
 * object length is extended to cover the run, and the read is logged.
 * The calling process's network-read statistic is bumped on every exit.
 *
 * Parameters (frame at 0x00E02CA6, `link.w A6,-0x68`):
 *   aste       (0x08,A6)
 *   segmap     (0x0C,A6)  (A4)
 *   ppn_array  (0x10,A6)  (D6)
 *   start_page (0x14,A6)  word
 *   count      (0x16,A6)  word
 *   flags      (0x18,A6)  ONE BYTE, READ_AHEAD's `flags`
 *   status     (0x1A,A6)  (D7)
 * Locals: (-0x28) the 32-byte page request (only uid and page_num are
 * written), (-0x38) dtm, (-0x40) clock, (-0x30) acl_info, (-0x48)
 * NETBUF_$GET_DAT's address, (-0x4E) allocated, (-0x50) page size,
 * (-0x5A) no_read_ahead, (-0x68) the stat increment.
 *
 * Returns D0w = pages read.
 *
 * Original address: 0x00E02CA6 (654 bytes).  No A5.
 */

#include "ast/ast_internal.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"
#include "netbuf/netbuf.h"
#include "network/network.h"

/*
 * ast_$read_area_pages_net_log - the nested NETLOG helper at 0x00E02C52
 *                                (84 bytes)
 *
 * Two stack arguments of its own - the page count word at (0x8,A6) and
 * the null-pages boolean byte at (0xA,A6) - plus the parent's aste
 * ((0x8,A2)) and start_page ((0x14,A2)) through the static link
 * (`movea.l (A6),A2` at 0x00E02C5E), passed explicitly here.
 *
 *   00e02c5a  move.b (0xa,A6),D0b / bpl     ; null pages -> kind 8, else 9
 *   00e02c72  clr.w -(SP)                   ; p8 = 0
 *   00e02c74  move.w (0x8,A6),-(SP)         ; p7 = count
 *   00e02c78  clr.l -(SP)                   ; p5 = p6 = 0
 *   00e02c7a  move.w (0x14,A2),-(SP)        ; p4 = start_page
 *   00e02c82  move.w (0xc,A0),-(SP)         ; p3 = aste->segment
 *   00e02c8e  pea (0x10,A4)                 ; p2 = &aote->uid
 *   00e02c92  move.w (-0xa,A6),-(SP)        ; p1 = kind
 */
static void ast_$read_area_pages_net_log(int16_t count, int8_t null_pages,
                                         const aste_t *aste,
                                         uint16_t start_page)
{
    uint16_t kind = (null_pages < 0) ? 8 : 9;

    NETLOG_$LOG_IT(kind, (uint32_t *)&aste->aote->uid, aste->segment,
                   start_page, 0, 0, (uint16_t)count, 0);
}

int16_t ast_$read_area_pages_network(aste_t *aste, uint32_t *segmap,
                                     uint32_t *ppn_array, uint16_t start_page,
                                     uint16_t count, uint8_t flags,
                                     status_$t *status)
{
    aote_t *aote;                       /* A3 */
    int16_t allocated;                  /* D2 / (-0x4E,A6) */
    int16_t pages_read;                 /* D3 */
    int8_t no_read_ahead;               /* (-0x5A,A6) */
    int8_t null_pages;                  /* D2b */
    uint16_t page_size;                 /* D3w / (-0x50,A6) */
    network_$page_request_t request;    /* (-0x28,A6): reserved left as is */
    clock_t dtm;                        /* (-0x38,A6) */
    clock_t clock;                      /* (-0x40,A6) */
    clock_t acl_info;                   /* (-0x30,A6) */
    uint32_t buf_addr;                  /* (-0x48,A6) */
    int32_t end_offset;                 /* D1 */
    uint32_t *ent;                      /* A4 */
    int16_t i;

    /* 0x00E02CBA..0x00E02CD2: allocate_pages(count, 1, array) */
    aote = aste->aote;
    allocated = ast_$allocate_pages((int16_t)count, 1, ppn_array);

    /* 0x00E02CD4..0x00E02CE2: move.w (0xbe,A3) / btst.l #4 = flags bit 4
     * (TOUCHED) -> no read-ahead; then cleared */
    no_read_ahead = (aote->flags & AOTE_FLAG_TOUCHED) ? -1 : 0;
    aote->flags &= (uint8_t)~AOTE_FLAG_TOUCHED;

    /* 0x00E02CE8..0x00E02CF4 */
    ML_$UNLOCK(PMAP_LOCK_ID);

    /* 0x00E02CF6..0x00E02D18: each frame's buffer (ppn << 10) to NETBUF */
    for (i = 0; i < allocated; i++) {
        NETBUF_$RTN_DAT(ppn_array[i] << 10);
    }

    /* 0x00E02D1C..0x00E02D3C: uid, then page = (segment << 5) + start */
    request.uid.high = aote->uid.high;
    request.uid.low = aote->uid.low;
    request.page_num = ((uint32_t)aste->segment << 5) + (uint32_t)start_page;

    /* 0x00E02D44..0x00E02D54: 1 << ((aote+0x9D & 0xF) + 9); aote+0x9D is
     * byte 1 of the obj_loc reserved_00 word */
    page_size = (uint16_t)(1u << (((aote->obj_uid.high >> 16) & 0xF) + 9));

    /* 0x00E02D58..0x00E02D88: 36 bytes of arguments; `move.b (0x18,A6)`
     * and `move.b (-0x5a,A6)` are the two byte flags */
    pages_read = NETWORK_$READ_AHEAD(&aote->obj_loc_net, &request, ppn_array,
                                     page_size, allocated, no_read_ahead,
                                     flags, &dtm, &clock, &acl_info, status);

    /* 0x00E02D8A..0x00E02DB6: allocated - (pages_read + 1) + 1 frames
     * come back from NETBUF and are freed; bmi skips */
    for (i = pages_read; i < allocated; i++) {
        NETBUF_$GET_DAT(&buf_addr);
        MMAP_$FREE(buf_addr >> 10);
    }

    /* 0x00E02DBA: nothing read (signed) */
    if (pages_read <= 0) {
        ML_$LOCK(PMAP_LOCK_ID);                     /* 0x00E02F06 */
        goto stats;
    }

    /* 0x00E02DC0..0x00E02DCC */
    *status = status_$ok;
    null_pages = (ppn_array[0] == 0) ? -1 : 0;      /* seq D2b */

    /* 0x00E02DCE..0x00E02DF4: null pages - fetch frames from NETBUF */
    if (null_pages < 0) {
        for (i = 0; i < pages_read; i++) {
            NETBUF_$GET_DAT(&buf_addr);
            ppn_array[i] = buf_addr >> 10;
        }
    }

    /* 0x00E02DF8..0x00E02E04 */
    ML_$LOCK(PMAP_LOCK_ID);

    /* 0x00E02E06..0x00E02E36: per page read */
    ent = segmap;
    for (i = 0; i < pages_read; i++) {
        if (null_pages < 0) {
            ZERO_PAGE(ppn_array[i]);
            *ent |= 0x00400000u;                    /* bset.b #6,(0x1,A4) */
        }
        *ent &= 0xFFC00000u;
        *ent |= 1u;
        ent++;
    }

    /* 0x00E02E3A..0x00E02E6C: clocks from the reply, or the local clock */
    if (dtm.high == 0) {
        TIME_$CLOCK(&clock);
    } else {
        aote->dtu_high = dtm.high;
        aote->dtu_low = dtm.low;
        aote->dtm_high = acl_info.high;
        aote->dtm_low = acl_info.low;
        aote->dta_high = clock.high;
        aote->dta_low = clock.low;
    }

    /* 0x00E02E72..0x00E02E9E: byte offset of the last page read; the
     * compare against the length is SIGNED (blt) */
    end_offset = (int32_t)(((uint32_t)start_page + (uint32_t)(int32_t)pages_read - 1 +
                            ((uint32_t)aste->segment << 5)) << 10);
    if (end_offset >= (int32_t)aote->length) {
        /* 0x00E02EA0..0x00E02EE0: extend to the next page boundary */
        aote->length = (uint32_t)end_offset + 0x400;
        if (dtm.high == 0) {
            aote->dta_high = clock.high;
            aote->dta_low = clock.low;
            aote->dtm_high = clock.high;
            aote->dtm_low = clock.low;
        }
    } else {
        /* 0x00E02EE2..0x00E02EEE */
        if (dtm.high == 0) {
            aote->dtu_high = clock.high;
            aote->dtu_low = clock.low;
        }
    }

    /* 0x00E02EF4..0x00E02F04: `move.b D2b` then `move.w D3w` */
    if (NETLOG_$OK_TO_LOG < 0) {
        ast_$read_area_pages_net_log(pages_read, null_pages, aste, start_page);
    }

stats:
    /* 0x00E02F12..0x00E02F24: PROC1_$STATS entry pid, longword +0x0C
     * (A0 = 0xE25D20 is entry 1; -0x4 + pid*16) */
    PROC_STATS_BASE[PROC1_$CURRENT * 4 + 3] += (uint32_t)(int32_t)pages_read;

    /* 0x00E02F28 */
    return pages_read;
}
