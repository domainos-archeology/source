/*
 * AST_$COPY_AREA - Copy one segment of an area into another segment
 *
 * Walks the 32 pages of the source segment and builds the destination
 * segment's map from them:
 *
 *   - a page that is INSTALLED (segment map bit 30) is wired, mapped at
 *     the caller's `buffer` window with MMU_$INSTALL_LIST, and copied
 *     page by page into freshly allocated frames that are mapped one at a
 *     time at the wired AST_$ZERO_BUFF address (0x00E03AF4..0x00E03C3C);
 *   - a page that is EMPTY (no disk address) leaves a zero destination
 *     entry (0x00E03F44..0x00E03F5C);
 *   - a run of pages that live ON DISK is marked in transition, read in
 *     one go - from the diskless partner with NETWORK_$READ_AHEAD when the
 *     area's remote volume index is non-zero, else from the local volume
 *     with DISK_$READ_MULTI - and the frames that arrived are installed
 *     in the destination map (0x00E03C40..0x00E03F42).
 *
 * After each run the new frames are installed in the working set of `pid`
 * with MMAP_$INSTALL_PAGES and the destination ASTE is marked dirty and
 * has its page count raised.  A status left non-zero by a read ends the
 * copy early.  The PMAP in-transition eventcount is advanced whenever a
 * disk/network run was marked and waiters may be pending.
 *
 * Parameters (frame at 0x00E03A30, `link.w A6,-0x180`):
 *   partner_index (0x08,A6)  1-based area id (D2 * 0x30 indexes the table)
 *   pid           (0x0A,A6)  word handed to MMAP_$INSTALL_PAGES
 *   src_aste      (0x0C,A6)
 *   dst_aste      (0x10,A6)
 *   start_seg     (0x14,A6)  word; << 5 gives the page base for the reads
 *   buffer        (0x16,A6)  a 32-page virtual window (longword VA)
 *   status        (0x1A,A6)
 * Frame cells: (-0x15C) source map cursor, (-0x154) destination map
 * cursor, (-0x14C) buffer VA cursor, (-0x17C) partner*0x30, (-0x16C)
 * is_remote, (-0x168) destination segment index, (-0x124) start_seg<<5,
 * (-0x100) wired source ppns, (-0x80) new ppns, (-0x120) the 12-byte page
 * request head, (-0x130) one clock_t passed three times, (-0x128)
 * NETBUF_$GET_DAT's address, (-0x148)/(-0x144) queue block head/tail,
 * (-0x160) pages read, (-0x150) the temporary map VA, (-0x180) the stat
 * increment.
 *
 * Original address: 0x00E03A30 (1416 bytes), A5 = 0xE1DC80 (AST_ block;
 * (0x44C,A5) is AST_$PMAP_IN_TRANS_EC).
 */

#include "ast/ast_internal.h"
#include "misc/misc.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"
#include "netbuf/netbuf.h"
#include "network/network.h"
#include "area/area.h"
#include "anon/anon.h"
#include "os/os.h"
#include "disk/disk.h"
/*
 * TODO(source-jtfb): disk_io_req_t is defined in disk/disk_internal.h;
 * this routine fills the queue blocks DISK_$GET_QBLKS hands out (daddr,
 * ppn, header), so the record belongs in disk/disk.h.  disk/ was being
 * edited by another agent when this file was re-emitted.
 */
#include "disk/disk_internal.h"

/*
 * Status cells passed to CRASH_SYSTEM by `pea (d,PC)`.
 */
/*
 * 0x00E03B8A: pea (-0x648,PC) -> 0x00E03544 and 0x00E03EAA: pea
 * (-0x968,PC) -> 0x00E03544, jsr CRASH_SYSTEM.  Image bytes 00 06 00 0C
 * (mmap "bad install").  Shared with AST_$PMAP_ASSOC, AST_$ASSOC_AREA and
 * AST_$TOUCH.
 */
static const status_$t mmap_$bad_install_00e03544 = 0x0006000C;
/*
 * 0x00E03DE2: pea (0x1d4,PC) -> 0x00E03FB8, jsr CRASH_SYSTEM at
 * 0x00E03DE6.  Image bytes 00 32 00 0A (area "internal error"): a source
 * page marked on-disk whose disk address is zero.
 */
static const status_$t area_$internal_error_00e03fb8 = 0x0032000A;

void AST_$COPY_AREA(uint16_t partner_index, uint16_t pid, aste_t *src_aste,
                    aste_t *dst_aste, uint16_t start_seg, char *buffer,
                    status_$t *status)
{
    uint32_t *src_row;              /* (-0x15C,A6) */
    uint32_t *dst_row;              /* (-0x154,A6) */
    uint32_t buffer_va;             /* (-0x14C,A6) */
    area_$entry_t *area;            /* 0xD94C00 + (-0x17C,A6) */
    int8_t is_remote;               /* (-0x16C,A6) */
    uint16_t dst_seg;               /* (-0x168,A6) */
    uint32_t start_off;             /* (-0x124,A6) */
    int8_t pending;                 /* D3b: an advance of the EC is owed */
    uint16_t page;                  /* D7w */
    uint16_t got;                   /* D4w */
    uint16_t run;                   /* D5w */
    uint32_t new_ppns[32];          /* (-0x80,A6) */
    uint32_t wired_ppns[32];        /* (-0x100,A6) */
    network_$page_request_t page_req;   /* (-0x120,A6): 12 bytes set */
    clock_t clock_scratch;          /* (-0x130,A6) */
    uint32_t buf_addr;              /* (-0x128,A6) */
    uint32_t qblk_head;             /* (-0x148,A6) */
    uint32_t qblk_tail;             /* (-0x144,A6) */
    int16_t pages_read;             /* (-0x160,A6) */
    uint32_t temp_va;               /* (-0x150,A6) */
    uint32_t page_base;             /* D6 */
    uint32_t *ent;                  /* A0 / A2 run cursors */
    uint32_t e;
    uint32_t ppn;
    uint16_t p;
    mmape_t *mmape;
    uint32_t *pft;
    disk_io_req_t *req;
    int16_t i;
    int16_t n;

    /* 0x00E03A3E..0x00E03AB4 */
    *status = status_$ok;
    src_row = (uint32_t *)PMAP_SEGMAP_ROW(src_aste->seg_index);
    dst_row = (uint32_t *)PMAP_SEGMAP_ROW(dst_aste->seg_index);
    buffer_va = ARCH_PTR_TO_VA(buffer);
    area = AREA_ID_TO_ENTRY(partner_index);
    /* tst.w (-0x8,A1,D2) = entry+0x28, sne */
    is_remote = (area->remote_volx != 0) ? -1 : 0;
    pending = 0;
    dst_seg = dst_aste->seg_index;
    start_off = (uint32_t)start_seg << 5;

    /* 0x00E03AB8..0x00E03AC8 */
    ML_$LOCK(PMAP_LOCK_ID);
    page = 0;
    goto check;

top:
    /*
     * 0x00E03AE2..0x00E03AE8 with 0x00E03ACC..0x00E03ADE: while the source
     * entry is in transition, pay any owed advance and wait.
     */
    while ((int32_t)*src_row < 0) {
        if (pending < 0) {
            EC_$ADVANCE(&AST_$PMAP_IN_TRANS_EC);
            pending = 0;
        }
        ast_$wait_for_page_transition();
    }
    e = *src_row;

    /* 0x00E03AEA..0x00E03AF0: btst.l #0xe on the high word = bit 30 */
    if (e & SEGMAP_VALID) {
        /* ---------------------------------------------------------------
         * Installed source pages: 0x00E03AF4..0x00E03C3C
         * --------------------------------------------------------------- */
        got = 0;
        ent = src_row;
        p = page;
        do {
            /* 0x00E03AFE..0x00E03B14 */
            ppn = *ent & 0xFFFF;
            wired_ppns[got] = ppn;
            MMAP_$WIRE(ppn);
            *ent |= SEGMAP_WIRED;               /* bset.b #0x5,(A2)+ = bit 29 */
            ent++;
            got++;
            p++;
            /* 0x00E03B1A..0x00E03B2A: stop past page 31, at a page that is
             * not installed, or at one in transition */
            if (p > 0x1F) {
                break;
            }
            e = *ent;
            if ((e & SEGMAP_VALID) == 0) {
                break;
            }
        } while ((int32_t)e >= 0);

        /*
         * 0x00E03B2C..0x00E03B48: map the wired pages at the buffer window.
         * The two words pushed before the VA (7, then the ASID) are the
         * asid and prot words; the `subq.l #0x2,SP` keeps the 14-byte
         * frame long-aligned (mmu/mmu.h, MMU_$INSTALL_LIST).
         */
        MMU_$INSTALL_LIST(got, wired_ppns, buffer_va, PROC1_$AS_ID, 7);

        /* 0x00E03B4C..0x00E03B58: count and min_count are both `got` */
        ast_$allocate_pages((int16_t)got, (int16_t)got, new_ppns);

        /* 0x00E03B5A..0x00E03C38: dbf over got - 1, skipped when got == 0 */
        for (i = 0; i < (int16_t)got; i++) {
            mmape = MMAPE_FOR_VPN(new_ppns[i]);
            temp_va = ARCH_PTR_TO_VA(AST_$ZERO_BUFF);       /* 0xFF8C00 */

            /* 0x00E03B84..0x00E03B94: tst.b (-0x1ffb,A2) = flags1 bit 7 */
            if ((int8_t)mmape->flags1 < 0) {
                CRASH_SYSTEM(&mmap_$bad_install_00e03544);
            }
            /* 0x00E03B96..0x00E03BB6 */
            mmape->wire_count = 0;
            mmape->flags2 |= MMAPE_FLAG2_MODIFIED;   /* bset.b #6,+0x09 */
            mmape->flags1 |= MMAPE_FLAG1_IMPURE;     /* bset.b #6,+0x05 */
            mmape->flags2 |= MMAPE_FLAG2_ON_DISK;    /* bset.b #7,+0x09 */
            mmape->segment = dst_seg;
            mmape->seg_offset = (uint8_t)page;
            mmape->disk_addr = 0;

            /* 0x00E03BBA..0x00E03BC8: low word := ppn, bits 30 and 29 */
            *dst_row = (*dst_row & 0xFFFF0000u) | (new_ppns[i] & 0xFFFF);
            *dst_row |= SEGMAP_VALID;
            *dst_row |= SEGMAP_WIRED;

            /* 0x00E03BCC..0x00E03BE2: MMU_$INSTALL(ppn, 0xFF8C00,
             * ASID:0x16) - the ASID word is the high half of the flags */
            MMU_$INSTALL(new_ppns[i], temp_va, PROC1_$AS_ID, 0x16);

            /* 0x00E03BE6..0x00E03BF2: ori.w #0x6000 on the PFT low word;
             * the index is the ppn's low word << 2 (a 16-bit shift) */
            pft = PFT_FOR_PPN((uint16_t)(new_ppns[i] & 0xFFFF));
            *pft |= 0x00006000u;

            /* 0x00E03BF8..0x00E03C0A: OS_$DATA_COPY(buffer, temp, 0x400) */
            OS_$DATA_COPY(ARCH_VA_TO_PTR(buffer_va), ARCH_VA_TO_PTR(temp_va),
                          0x400);

            /* 0x00E03C0E..0x00E03C22 */
            MMU_$REMOVE(new_ppns[i]);
            MMAP_$UNWIRE(wired_ppns[i]);

            /* 0x00E03C24..0x00E03C36 */
            page++;
            dst_row++;
            src_row++;
            buffer_va += 0x400;
        }
        goto install_tail;                                  /* 0x00E03C3C */
    }

    /* 0x00E03C40..0x00E03C48: not installed and no disk address */
    if ((e & 0x7FFFFF) == 0) {
        /* 0x00E03F44..0x00E03F5C */
        *dst_row = 0;
        dst_row++;
        src_row++;
        buffer_va += 0x400;
        page++;
        goto check;
    }

    /* -------------------------------------------------------------------
     * A run of on-disk source pages: 0x00E03C4C..0x00E03F42
     * ------------------------------------------------------------------- */
    run = 0;
    ent = src_row;
    p = page;
    do {
        /* 0x00E03C50..0x00E03C58: bset.b #0x7,(A0)+ = bit 31 */
        *ent |= SEGMAP_IN_TRANS;
        ent++;
        run++;
        p++;
        /* 0x00E03C5A..0x00E03C7A: stop past page 31, at 32 pages, at an
         * installed page, one in transition, or one with no disk address */
        if (p > 0x1F) {
            break;
        }
        if (run >= 0x20) {
            break;
        }
        e = *ent;
        if (e & SEGMAP_VALID) {
            break;
        }
        if ((int32_t)e < 0) {
            break;
        }
    } while ((e & 0x7FFFFF) != 0);

    /* 0x00E03C7C..0x00E03C9E */
    ast_$allocate_pages((int16_t)run, (int16_t)run, new_ppns);
    page_req.uid.high = ANON_$UID.high;         /* move.l (0xe17414).l */
    ML_$UNLOCK(PMAP_LOCK_ID);

    /* 0x00E03CA0 */
    if (is_remote < 0) {
        /* 0x00E03CA8..0x00E03CD2 */
        page_req.uid.low = (uint32_t)(uint16_t)area->remote_volx;
        page_base = (uint32_t)page + start_off + ((start_off + 0x100) >> 8);
        got = 0;

        /* 0x00E03CD4..0x00E03D58: dbf over run - 1, skipped when run == 0 */
        for (i = 0; i < (int16_t)run; i++) {
            /* 0x00E03CF0..0x00E03CFE */
            NETBUF_$RTN_DAT(new_ppns[i] << 10);
            /* 0x00E03D00..0x00E03D08: D2 is the 1-based index */
            page_req.page_num = (uint32_t)(uint16_t)(i + 1) + page_base - 1;
            /*
             * 0x00E03D0C..0x00E03D2E: 36 bytes of arguments; `pea
             * (-0x130,A6)` and two `move.l (SP),-(SP)` pass one clock cell
             * three times; `clr.l -(SP)` is both byte flags; count 1.
             */
            (void)NETWORK_$READ_AHEAD(&AREA_$PARTNER, &page_req,
                                      &new_ppns[i], AREA_$PARTNER_PKT_SIZE,
                                      1, 0, 0, &clock_scratch, &clock_scratch,
                                      &clock_scratch, status);
            /* 0x00E03D32..0x00E03D50: on failure take the buffer back and
             * remember its page in the slot the read did not fill */
            if (*status != status_$ok) {
                NETBUF_$GET_DAT(&buf_addr);
                new_ppns[i] = buf_addr >> 10;
                break;
            }
            got++;                                          /* 0x00E03D52 */
        }

        /* 0x00E03D5C..0x00E03D76: PROC1_$STATS[pid] + 0x0C */
        PROC1_$DATA.stats[PROC1_$CURRENT].stat[3] += (uint32_t)got;
    } else {
        /* 0x00E03D7E..0x00E03D90 */
        page_req.uid.low = (uint32_t)partner_index;
        page_req.page_num = (uint32_t)page + start_off;

        /* 0x00E03D94..0x00E03DA6: result slot discarded */
        DISK_$GET_QBLKS((int16_t)run, &qblk_head, &qblk_tail);

        /* 0x00E03DAA..0x00E03DC0: 12 bytes of the request into the head
         * block's header (moveq #0xb byte loop), then clr.b (0x30,A2) */
        req = (disk_io_req_t *)ARCH_VA_TO_PTR(qblk_head);
        req->header[0] = page_req.uid.high;
        req->header[1] = page_req.uid.low;
        req->header[2] = page_req.page_num;
        req->header[4] &= 0x00FFFFFFu;

        /* 0x00E03DC4..0x00E03DF8: one block per page of the run */
        ent = src_row;
        for (i = 0; i < (int16_t)run; i++) {
            req->daddr = *ent & 0x3FFFFF;
            if (req->daddr == 0) {
                CRASH_SYSTEM(&area_$internal_error_00e03fb8);
            }
            req->ppn = new_ppns[i];
            ent++;
            req = (disk_io_req_t *)ARCH_VA_TO_PTR(req->free_next);  /* (0x8,A2) */
        }

        /*
         * 0x00E03DFC..0x00E03E24: DISK_$READ_MULTI(area->volx, TRUE, TRUE,
         * head, tail, &pages_read, status); the two `st -(SP)` are single
         * bytes and the result slot is discarded.
         */
        DISK_$READ_MULTI((uint16_t)area->volx, -1, -1, qblk_head,
                         qblk_tail, &pages_read, status);
        got = (uint16_t)pages_read;                          /* 0x00E03E28 */

        /* 0x00E03E2C..0x00E03E3C: a clean call takes the head block's
         * status; any failure gets bit 31 set */
        if (*status == status_$ok) {
            req = (disk_io_req_t *)ARCH_VA_TO_PTR(qblk_head);
            *status = req->status;
            if (*status != status_$ok) {
                *status |= (status_$t)0x80000000u;
            }
        } else {
            *status |= (status_$t)0x80000000u;
        }

        /* 0x00E03E40..0x00E03E50: result slot discarded */
        DISK_$RTN_QBLKS((int16_t)run, qblk_head, qblk_tail);

        /* 0x00E03E54..0x00E03E6E: PROC1_$STATS[pid] + 0x08 */
        PROC1_$DATA.stats[PROC1_$CURRENT].stat[2] += (uint32_t)got;
    }

    /* 0x00E03E72..0x00E03E7E */
    ML_$LOCK(PMAP_LOCK_ID);

    /* 0x00E03E80..0x00E03F08: install the pages that arrived; dbf over
     * got - 1, skipped when got == 0 */
    for (i = 0; i < (int16_t)got; i++) {
        mmape = MMAPE_FOR_VPN(new_ppns[i]);
        if ((int8_t)mmape->flags1 < 0) {
            CRASH_SYSTEM(&mmap_$bad_install_00e03544);
        }
        mmape->wire_count = 0;
        mmape->flags2 |= MMAPE_FLAG2_MODIFIED;
        mmape->flags1 |= MMAPE_FLAG1_IMPURE;
        mmape->flags2 |= MMAPE_FLAG2_ON_DISK;
        mmape->segment = dst_seg;
        mmape->seg_offset = (uint8_t)page;
        mmape->disk_addr = 0;

        /* 0x00E03EDA..0x00E03EE4: low word := ppn, bit 30 (not wired) */
        *dst_row = (*dst_row & 0xFFFF0000u) | (new_ppns[i] & 0xFFFF);
        *dst_row |= SEGMAP_VALID;

        /* 0x00E03EE8..0x00E03EEE */
        pft = PFT_FOR_PPN((uint16_t)(new_ppns[i] & 0xFFFF));
        *pft |= 0x00006000u;

        /* 0x00E03EF4..0x00E03EF8: the source page is no longer in transition */
        *src_row &= ~SEGMAP_IN_TRANS;

        /* 0x00E03EFC..0x00E03F04 */
        page++;
        dst_row++;
        src_row++;
    }

    /* 0x00E03F0C..0x00E03F30: frames that were not filled go back; their
     * source entries also leave transition.  Iterations = run - got. */
    if (got != run) {
        ent = src_row;
        n = (int16_t)((int16_t)run - (int16_t)(got + 1));
        if (n >= 0) {
            for (i = 0; i <= n; i++) {
                MMAP_$FREE(new_ppns[got + i]);
                *ent &= ~SEGMAP_IN_TRANS;
                ent++;
            }
        }
    }

    /* 0x00E03F34..0x00E03F42 */
    buffer_va += (uint32_t)got << 10;
    pending = -1;

install_tail:
    /* 0x00E03F5E..0x00E03F7E */
    if (got != 0) {
        MMAP_$INSTALL_PAGES(new_ppns, got, pid);
        dst_aste->flags |= ASTE_FLAG_DIRTY;     /* bset.b #0x5,(0x12,A4) = bit 13 */
        dst_aste->page_count = (uint8_t)(dst_aste->page_count + (uint8_t)got);
    }
    /* 0x00E03F82..0x00E03F88: a read failure ends the copy */
    if (*status != status_$ok) {
        goto finish;
    }

check:
    /* 0x00E03F8A: cmpi.w #0x20,D7w / bcs */
    if (page < 0x20) {
        goto top;
    }

finish:
    /* 0x00E03F92..0x00E03FA8 */
    ML_$UNLOCK(PMAP_LOCK_ID);
    if (pending < 0) {
        EC_$ADVANCE(&AST_$PMAP_IN_TRANS_EC);
    }
}
