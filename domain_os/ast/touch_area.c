/*
 * AST_$TOUCH_AREA - fault a run of *area* pages in
 *
 * The area flavour of AST_$TOUCH.  Where AST_$TOUCH is handed an ASTE and
 * works inside one segment, AST_$TOUCH_AREA is handed the AREA id plus the
 * segment/page coordinates directly, because an area's backing store is
 * described by the AREA_$ENTRY (local volume index at +0x24, diskless
 * partner volume index at +0x28) rather than by an AOTE.
 *
 * Parameter layout recovered from the prologue (`link.w A6,-0x70`, arguments
 * from A6+0x08 upwards):
 *
 *   A6+0x08 word  area_id     0x00E03556; area_id*0x30 + 0xD94BD0 == the
 *                             AREA_$ENTRY (== AREA_ID_TO_ENTRY(area_id))
 *   A6+0x0A word  seg_index   0x00E03580; seg_index<<7 indexes PMAP_$SEGMAP
 *   A6+0x0C word  page        0x00E0355A; page<<2 is the byte offset of the
 *                             first segment-map entry
 *   A6+0x0E long  area_page   0x00E03672/0x00E037AA/0x00E03A02; the
 *                             area-relative page number (bste*32 + seg)
 *   A6+0x12 long  ppn_array   0x00E035A0; the caller's PPN list
 *   A6+0x16 long  status      0x00E0355E; cleared on entry
 *
 * AREA_$TOUCH (0x00E09648) reserves a 2-byte Pascal result slot at A6+0x1A
 * but this body never writes it, so AST_$TOUCH_AREA is a procedure.
 *
 * Original address: 0x00E03548
 */

#include "ast/ast_internal.h"
#include "misc/misc.h"
#include "mmap/mmap.h"
#include "mmu/mmu.h"
#include "disk/disk.h"
#include "anon/anon.h"
#include "proc1/proc1.h"

/*
 * Status cell passed to CRASH_SYSTEM by `pea (-0x3f4,PC)` at 0x00E03936
 * (effective address 0x00E03544, a constant longword in this module's own
 * code region just ahead of the entry point).  Shared with AST_$TOUCH,
 * AST_$PMAP_ASSOC and AST_$ASSOC_AREA.
 */
static const status_$t mmap_$bad_install_00e03544 = 0x0006000C;

/*
 * Segment-map entry bits, named for the instruction that tests or sets them.
 * The original addresses a segment-map entry as a big-endian longword: the
 * byte at (A2) is the most significant one and the word at (0x2,A2) is the
 * low half.  Everything below is therefore spelled with longword masks and
 * shifts rather than byte/word pointer casts, so the C is byte-order
 * independent.
 */
#define SEGMAP_W_VALID    0x4000      /* high word bit 14: btst.l #0xe     */
#define SEGMAP_W_END      0x2000      /* high word bit 13: btst.l #0xd     */
#define SEGMAP_L_IN_TRANS 0x80000000u /* bset.b #0x7,(A2) - MSB bit 7      */
#define SEGMAP_L_VALID    0x40000000u /* bset.b #0x6,(A2) - MSB bit 6      */
#define SEGMAP_L_REF      0x20000000u /* bset.b #0x5,(A2) - MSB bit 5      */

/* Disk-address masks: the code uses 0x3FFFFF in two places and 0x7FFFFF in
 * a third.  Both are reproduced verbatim rather than unified. */
#define SEGMAP_DADDR_22 0x003FFFFF /* 0x00E03612, 0x00E0375E */
#define SEGMAP_DADDR_23 0x007FFFFF /* 0x00E03968 */

void AST_$TOUCH_AREA(uint16_t area_id, uint16_t seg_index, int16_t page,
                     uint32_t area_page, uint32_t *ppn_array,
                     status_$t *status)
{
    /* A4 (-0x4c,A6): the AREA_$ENTRY for area_id. */
    area_$entry_t *entry;
    /* D1 (-0x68,A6) and D2w (-0x6a,A6): the two halves of the segment-map
     * byte offset, kept separately because the code recomputes the pointer
     * from them twice (0x00E037C6 and 0x00E038C4). */
    uint32_t seg_byte_off;
    int16_t page_byte_off;
    uint32_t *segmap;          /* A2 */
    uint32_t *ppn_out;         /* A3/A4 walking ppn_array */
    int16_t pages_done;        /* D5 after the read: pages actually obtained */
    int16_t pages_marked;      /* D3: pages marked in-transition/requested */
    uint16_t log_op;           /* (-0x5a,A6): NETLOG_$LOG_IT "kind" */
    /*
     * (-0x20,A6): the 32-byte NETWORK_$READ_AHEAD request record.  Only its
     * uid (+0x00) and page_num (+0x08) are initialised here.
     */
    network_$page_request_t req;
    clock_t clk;               /* (-0x3c,A6): all three timestamp outputs */
    int32_t qblk_head;         /* (-0x30,A6) */
    uint32_t qblk_tail;        /* (-0x2c,A6) */
    int16_t alloc_count;       /* D2 */
    int16_t pages_read[1];     /* (-0x58,A6): DISK_$READ_MULTI result */
    uint32_t dat_addr;         /* (-0x24,A6): NETBUF_$GET_DAT result */
    int16_t i;

    *status = status_$ok;                                  /* 0x00E03562 */

    /* 0x00E03576: lea (-0x30,A1,D0), A1 = 0xD94C00 == AREA_ID_TO_ENTRY. */
    entry = (area_$entry_t *)(uintptr_t)(AREA_TABLE_BASE +
                                         (uint32_t)area_id * AREA_ENTRY_SIZE -
                                         AREA_ENTRY_SIZE);

    seg_byte_off  = (uint32_t)seg_index * 0x80;            /* 0x00E03580 */
    page_byte_off = (int16_t)(page << 2);                  /* 0x00E03596 */

    /* 0x00E0359C: lea (-0x80,A4,D2w) - the segment map is 1-based in
     * seg_index for areas, hence the -0x80 bias. */
    segmap = (uint32_t *)((char *)SEGMAP_BASE + seg_byte_off - 0x80 +
                          page_byte_off);

    pages_done   = 0;                                      /* 0x00E035A4 */
    pages_marked = 0;

    /* 0x00E035AE: wait while the first page is in transition. */
    while ((int16_t)(*segmap >> 16) < 0) {
        ast_$wait_for_page_transition();                   /* 0x00E00C08 */
    }

    if (((uint16_t)(*segmap >> 16) & SEGMAP_W_VALID) != 0) {
        /*
         * ------------------------------------------------------------------
         * 0x00E035BA: the page is already installed - just re-reference it
         * and hand the PPNs back through MMAP_$RECLAIM.
         * ------------------------------------------------------------------
         */
        int16_t n = 0;                                     /* D2 */
        uint16_t ppn;
        uint16_t w;

        ppn_out = ppn_array;                               /* A3 */
        for (;;) {
            *segmap |= SEGMAP_L_REF;                       /* 0x00E035C0 */

ppn = (uint16_t)*segmap;                       /* 0x00E035C4 */
            /* 0x00E035CA: PFT_BASE[ppn] low word |= 0x2000 */
            PFT_BASE[ppn] |= 0x2000u;

            /* 0x00E035D6: clr.l D7 / move.w (0x2,A2),D7w - zero-extended. */
            *ppn_out++ = (uint32_t)(uint16_t)*segmap;
            n++;                                           /* 0x00E035DE */
            segmap++;

            /*
             * 0x00E035E2: `tst.w D2w / bgt` - the loop limit is compared
             * against zero, so with n >= 1 it always exits here.  Preserved
             * verbatim; this is what the shipped code does.
             */
            if (n > 0) {
                break;
            }
            w = (uint16_t)(*segmap >> 16);
            if ((w & SEGMAP_W_END) != 0) {
                break;
            }
            if ((int16_t)w < 0) {
                break;
            }
            if ((w & SEGMAP_W_VALID) == 0) {
                break;
            }
        }

        MMAP_$RECLAIM(ppn_array, n, 0);                    /* 0x00E03600 */
        AST_$WS_FLT_CNT += (uint32_t)(int32_t)n;           /* 0x00E0360A */
        return;                                            /* 0x00E0360E */
    }

    if ((*segmap & SEGMAP_DADDR_22) == 0) {
        /*
         * ------------------------------------------------------------------
         * 0x00E0361C: no backing store - hand out a zero-filled page.
         * ------------------------------------------------------------------
         */
        *segmap |= SEGMAP_L_IN_TRANS;
        ast_$allocate_pages(1, 1, ppn_array);              /* 0x00E00D46 */
        ZERO_PAGE(ppn_array[0]);                           /* 0x00E00EB0 */
        log_op = 8;                                        /* 0x00E0363C */
        req.uid.high = ANON_$UID.high;                     /* 0x00E03642 */
        req.uid.low  = (uint32_t)area_id;                  /* 0x00E0364A */
        pages_done   = 1;                                  /* 0x00E03654 */
        pages_marked = 1;
    } else {
        log_op = 2;                                        /* 0x00E0365C */

        if (entry->remote_volx != 0) {
            /*
             * --------------------------------------------------------------
             * 0x00E0366E: diskless - fetch the page from the partner node.
             * --------------------------------------------------------------
             */
            *segmap |= SEGMAP_L_IN_TRANS;
            pages_marked = 1;                              /* 0x00E03676 */
            /* 0x00E03672-0x00E03688: page_num = area_page +
             * ((area_page + 0x100) >> 8); stored twice by the compiler. */
            req.page_num = area_page + ((area_page + 0x100) >> 8);
            req.uid.high = ANON_$UID.high;                 /* 0x00E0368C */
            req.uid.low  = (uint32_t)(uint16_t)entry->remote_volx;

            /* 0x00E0369E: two D3 word pushes -- count and min_count are
             * both pages_marked. */
            ast_$allocate_pages(pages_marked, pages_marked, ppn_array);
            NETBUF_$RTN_DAT(ppn_array[0] << 10);           /* 0x00E0F046 */
            ML_$UNLOCK(PMAP_LOCK_ID);                      /* 0x00E036C0 */

            /*
             * 0x00E036CE: the same 6-byte buffer at (-0x3c,A6) is pushed for
             * all three timestamp outputs (`pea` once, then two
             * `move.l (SP),-(SP)`).
             */
            pages_done = NETWORK_$READ_AHEAD(&AREA_$PARTNER, &req, ppn_array,
                                             (uint16_t)AREA_$PARTNER_PKT_SIZE,
                                             pages_marked, 0, 0,
                                             &clk, &clk, &clk, status);
            if (pages_done == 0) {                         /* 0x00E036FC */
                NETBUF_$GET_DAT(&dat_addr);                /* 0x00E0EFA4 */
                MMAP_$FREE(dat_addr >> 10);                /* 0x00E0CAC2 */
            }
            ML_$LOCK(PMAP_LOCK_ID);                        /* 0x00E0371E */

            /* 0x00E0373E: PROC_STATS_BASE[cur*4 + 3] += pages_done */
            PROC_STATS_BASE[(uint16_t)(PROC1_$CURRENT * 4) + 3] +=
                (uint32_t)(int32_t)pages_done;
        } else {
            /*
             * --------------------------------------------------------------
             * 0x00E03746: local disk - mark the run in transition, allocate
             * page frames and read them with DISK_$READ_MULTI.
             * --------------------------------------------------------------
             */
            uint16_t w;
            void *qb;

            for (;;) {
                *segmap |= SEGMAP_L_IN_TRANS;              /* 0x00E03746 */
                pages_marked++;
                segmap++;

                /*
                 * 0x00E0374E: `tst.w D3w / bgt` - as in the installed-page
                 * loop above, the limit is zero, so exactly one page is ever
                 * marked.  Preserved verbatim.
                 */
                if (pages_marked > 0) {
                    break;
                }
                w = (uint16_t)(*segmap >> 16);
                if ((w & SEGMAP_W_END) != 0) {
                    break;
                }
                if ((int16_t)w < 0) {
                    break;
                }
                if ((*segmap & SEGMAP_DADDR_22) == 0) {
                    break;
                }
                if ((w & SEGMAP_W_VALID) != 0) {
                    break;
                }
            }

            /* 0x00E0376E: count = pages_marked, min_count = 1 */
            alloc_count = ast_$allocate_pages(pages_marked, 1, ppn_array);
            ML_$UNLOCK(PMAP_LOCK_ID);                      /* 0x00E03780 */

            DISK_$GET_QBLKS(alloc_count, &qblk_head, &qblk_tail);

            /* 0x00E037A4: fill the head queue block's object identity. */
            qb = (void *)(uintptr_t)qblk_head;
            *(uint32_t *)((char *)qb + 0x28) = area_page;
            *(uint32_t *)((char *)qb + 0x20) = ANON_$UID.high;
            *(uint32_t *)((char *)qb + 0x24) = (uint32_t)area_id;
            *(uint8_t  *)((char *)qb + 0x30) = 0;

            /* 0x00E037C6: recompute the segment-map pointer from scratch. */
            segmap = (uint32_t *)((char *)SEGMAP_BASE + seg_byte_off - 0x80 +
                                  page_byte_off);

            /* 0x00E037E2: one queue block per allocated page. */
            ppn_out = ppn_array;
            for (i = (int16_t)(alloc_count - 1); i >= 0; i--) {
                *(uint32_t *)((char *)qb + 0x14) = *ppn_out++;
                *(uint32_t *)((char *)qb + 0x04) = *segmap++ & SEGMAP_DADDR_22;
                qb = *(void **)((char *)qb + 0x08);
            }

            /*
             * 0x00E03806: the two `st -(SP)` pushes are Domain booleans
             * (0xFF == true) occupying one stack word each.
             */
            DISK_$READ_MULTI((uint16_t)entry->volx, 0xFF, 0xFF,
                             qblk_head, qblk_tail, pages_read, status);
            pages_done = pages_read[0];                    /* 0x00E03828 */

            ML_$LOCK(PMAP_LOCK_ID);                        /* 0x00E0382C */
            DISK_$RTN_QBLKS(alloc_count, qblk_head, qblk_tail);

            /*
             * 0x00E0384E: release the frames the disk never filled.  Pascal
             * `for k := pages_done+1 to alloc_count do free(ppn_array[k])`,
             * with the array indexed 1-based (hence the [-4] displacement in
             * the loop body at 0x00E0386E).
             */
            if (pages_done < alloc_count) {
                for (i = (int16_t)(pages_done + 1); i <= alloc_count; i++) {
                    MMAP_$FREE(ppn_array[i - 1]);
                }
            }

            /* 0x00E0389A: PROC_STATS_BASE[cur*4 + 2] += pages_done */
            PROC_STATS_BASE[(uint16_t)(PROC1_$CURRENT * 4) + 2] +=
                (uint32_t)(int32_t)pages_done;
        }

        /* 0x00E0389E: mark a failed status by setting its top bit. */
        if (*status != status_$ok) {
            *status |= (status_$t)0x80000000;
        }
        /* 0x00E038AA: PROC_STATS_BASE[cur*4 + 0] += pages_done */
        PROC_STATS_BASE[(uint16_t)(PROC1_$CURRENT * 4) + 0] +=
            (uint32_t)(int32_t)pages_done;
    }

    /*
     * ----------------------------------------------------------------------
     * 0x00E038C4: common tail - install whatever came back.
     * ----------------------------------------------------------------------
     */
    segmap = (uint32_t *)((char *)SEGMAP_BASE + seg_byte_off - 0x80 +
                          page_byte_off);

    if (pages_done < pages_marked) {                       /* 0x00E038DA */
        ast_$clear_transition_bits(
            (uint32_t *)((char *)SEGMAP_BASE + seg_byte_off - 0x80 +
                         (int16_t)((pages_done + page) << 2)),
            (uint16_t)(pages_marked - pages_done));
    }

    if (*status == status_$ok) {                           /* 0x00E038FA */
        ppn_out = ppn_array;                               /* 0x00E0391C */
        /* 0x00E03904: `move.w D5w,D0w / subq.w #1 / bmi` - skipped entirely
         * when pages_done == 0, but MMAP_$INSTALL_LIST is still called. */
        for (i = 1; i <= pages_done; i++) {
            uint32_t vpn = *ppn_out++;
            mmape_t *m = &MMAPE_BASE[vpn];
            uint16_t ppn;

            if ((int8_t)m->flags1 < 0) {                   /* 0x00E03930 */
                CRASH_SYSTEM(&mmap_$bad_install_00e03544);
            }
            m->wire_count = 0;                             /* 0x00E03942 */
            m->flags2 &= (uint8_t)~MMAPE_FLAG2_MODIFIED;   /* 0x00E03946 */
            m->flags1 |= MMAPE_FLAG1_IMPURE;               /* 0x00E0394C */
            m->flags2 |= MMAPE_FLAG2_ON_DISK;              /* 0x00E03952 */
            /* 0x00E03958: byte arithmetic, deliberately truncating. */
            m->seg_offset = (uint8_t)((uint8_t)i + (uint8_t)page - 1);
            m->segment = seg_index;                        /* 0x00E03962 */
            m->disk_addr = *segmap & SEGMAP_DADDR_23;      /* 0x00E03968 */

            /* 0x00E03974: plant the PPN in the segment map's low word. */
            *segmap = (*segmap & 0xFFFF0000u) | (uint32_t)(uint16_t)vpn;
            *segmap |= SEGMAP_L_REF;                       /* 0x00E0397C */

            /* 0x00E03980: PFT_BASE[ppn] low word = (w & 0xBFFF) | 0x2000 */
            ppn = (uint16_t)*segmap;
            PFT_BASE[ppn] = (PFT_BASE[ppn] & 0xFFFFBFFFu) | 0x2000u;

            *segmap |= SEGMAP_L_VALID;                     /* 0x00E03998 */
            *segmap &= ~SEGMAP_L_IN_TRANS;                 /* 0x00E0399C */
            segmap++;
        }

        MMAP_$INSTALL_LIST(ppn_array, (uint16_t)pages_done, 0);

        /*
         * 0x00E039BA: `0xEC5400 - 4 + seg_index*0x14` is the page_count byte
         * (+0x10) of ASTE_BASE[seg_index - 1] - the segment map's 1-based
         * indexing again.
         */
        ASTE_BASE[seg_index - 1].page_count += (uint8_t)pages_done;
    }

    AST_$PAGE_FLT_CNT += 1;                                /* 0x00E039D2 */

    if (NETLOG_$OK_TO_LOG < 0) {                           /* 0x00E039D6 */
        NETLOG_$LOG_IT(log_op, (uint32_t *)&req.uid,
                       (uint16_t)(area_page >> 5),
                       (uint16_t)page,
                       (uint16_t)ppn_array[0],
                       (uint16_t)pages_done,
                       (uint16_t)(entry->remote_volx != 0 ? 1 : 0),
                       0);
    }

    EC_$ADVANCE(&AST_$PMAP_IN_TRANS_EC);                   /* 0x00E03A1C */
}
