/*
 * AST_$TOUCH - Fault a run of a segment's pages in
 *
 * The page-fault core.  After the access checks (a local object with
 * OS-only access is refused to a type-8 process; a concurrency token
 * that is neither 0, 1 nor `mode` is refused unless attribute-flags bit
 * 11 allows it) the AOTE is marked busy and the ASTE locked, and the
 * run [page, page+count) is clipped to the segment.  Starting at the
 * first entry (after any transition):
 *
 *   - INSTALLED: consecutive installed, unwired entries are wired (bit 29)
 *     and their frames returned; real frames are reclaimed into the
 *     working set with MMAP_$RECLAIM.  Counted in AST_$WS_FLT_CNT.
 *   - marked with bit 22: the consecutive run of such entries is put in
 *     transition and handed to ast_$count_valid_pages (kind 8 in the log).
 *   - otherwise the run is clipped to the object's length (beyond it: an
 *     error unless flags bit 0, and then a grow-ahead of
 *     AST_$GROW_AHEAD_CNT pages for a full-segment request or one page,
 *     unless flags bit 1); a run of local entries with no disk address
 *     gets blocks from ast_$setup_page_read and is then counted by
 *     ast_$count_valid_pages; any other run is put in transition and
 *     read - from the home node or from disk (kind 2), the pages counted
 *     in the process's read statistic (flags bit 3 selects which).
 *
 * Entries put in transition but not filled are released; no page at all
 * sets bit 31 of the status.  Read pages are installed: their MMAPEs
 * are filled (IMPURE when flags bit 3), the entries get the frame with
 * bits 29 and 30 and leave transition, the PFT words are marked, the
 * frames join the working set (MMAP_$INSTALL_LIST, wired when flags bit
 * 5), the ASTE page count grows and PMAP waiters are woken.  Counted in
 * AST_$PAGE_FLT_CNT and logged.
 *
 * Parameters (frame at 0x00E030C0, `link.w A6,-0x34`):
 *   aste      (0x08,A6)
 *   mode      (0x0C,A6)  longword (D0)
 *   page      (0x10,A6)  word (D3)
 *   count     (0x12,A6)  word (D2)
 *   ppn_array (0x14,A6)  up to 32 frames out
 *   status    (0x18,A6)
 *   flags     (0x1C,A6)  word; bits 0, 1, 3, 5 of its low byte are read
 * (-0x24,A6) aote, (-0x16,A6) the log kind.  Returns D0w = pages.
 *
 * Original address: 0x00E030C0 (1156 bytes), A5 = 0xE1DC80 (AST_ block):
 *   (0x44C,A5) AST_$PMAP_IN_TRANS_EC  (0x458,A5) AST_$WS_FLT_CNT
 *   (0x45C,A5) AST_$PAGE_FLT_CNT      (0x46C,A5) AST_$GROW_AHEAD_CNT
 */

#include "ast/ast_internal.h"
#include "misc/misc.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"

/*
 * Status cells passed to CRASH_SYSTEM by `pea (d,PC)`.
 */
/*
 * 0x00E03414: pea (-0x256a,PC) -> 0x00E00EAC, jsr CRASH_SYSTEM at
 * 0x00E03418.  Image bytes 00 05 00 03 (pmap "mismatch").  Shared with
 * AST_$PMAP_ASSOC, ast_$allocate_pages and AST_$ASSOC_AREA.
 */
static const status_$t pmap_$mismatch_00e00eac = 0x00050003;
/*
 * 0x00E03434: pea (0x10e,PC) -> 0x00E03544, jsr CRASH_SYSTEM at
 * 0x00E03438.  Image bytes 00 06 00 0C (mmap "bad install").  Shared with
 * AST_$PMAP_ASSOC, AST_$ASSOC_AREA and AST_$COPY_AREA.
 */
static const status_$t mmap_$bad_install_00e03544 = 0x0006000C;

uint16_t AST_$TOUCH(aste_t *aste, uint32_t mode, uint16_t page, uint16_t count,
                    uint32_t *ppn_array, status_$t *status, uint16_t flags)
{
    aote_t *aote;               /* (-0x24,A6) */
    uint32_t concurrency;       /* D1 */
    uint16_t avail;             /* D4w */
    uint32_t *row;              /* A3: the segment's unbiased row base */
    uint32_t *entry;            /* A2 */
    uint32_t *ent;              /* A0 */
    uint32_t *out;              /* A1 */
    uint16_t touched;           /* D2w */
    uint16_t run;               /* D5w */
    uint16_t kind;              /* (-0x16,A6) */
    uint32_t page_no;           /* D0 */
    uint32_t last_page;         /* D1 */
    uint32_t in_file;           /* D5 */
    uint32_t ppn;               /* D4 */
    mmape_t *mmape;             /* A2 - 0x2000 */
    uint32_t *pft;
    int16_t i;

    /* 0x00E030DA..0x00E030E4 */
    *status = status_$ok;
    aote = aste->aote;

    /* 0x00E030EE..0x00E03144: access checks for a local object */
    if (aote->remote_flag >= 0) {
        if (aote->access_flags < 0 && PROC1_$TYPE[PROC1_$CURRENT] == 8) {
            *status = status_$ast_only_local_access_allowed;    /* 0x3000A */
            return 0;                                           /* 0x00E0327E */
        }
        concurrency = aote->blocks;
        if (concurrency != 0 && concurrency != mode && concurrency != 1 &&
            (aote->attr_flags_hi & 0x08) == 0) {
            *status = status_$pmap_read_concurrency_violation;  /* 0x5000A */
            return 0;
        }
    }

    /* 0x00E03148..0x00E0315C: busy, locked (bit 14), clip to the segment */
    aote->flags |= AOTE_FLAG_BUSY;
    aste->flags |= ASTE_FLAG_LOCKED;
    avail = (uint16_t)(0x20 - page);
    if (avail > count) {
        avail = count;
    }

    /* 0x00E0315E..0x00E03174 */
    row = (uint32_t *)((char *)SEGMAP_BASE + ((uint32_t)aste->seg_index << 7));
    entry = (uint32_t *)((char *)row + (uint16_t)(page << 2) - 0x80);

    /* 0x00E03178..0x00E03180 */
    while ((int32_t)*entry < 0) {
        ast_$wait_for_page_transition();
    }

    /* 0x00E03182..0x00E03188 */
    if (*entry & SEGMAP_VALID) {
        /* 0x00E0318A..0x00E031B6: wire consecutive installed, unwired,
         * settled entries and return their frames */
        ent = entry;
        out = ppn_array;
        touched = 0;
        do {
            *ent |= SEGMAP_WIRED;                   /* bset.b #0x5,(A0) */
            *out++ = *ent & 0xFFFF;
            touched++;
            ent++;
            if (touched >= avail) {
                break;
            }
            if (*ent & SEGMAP_WIRED) {
                break;
            }
            if ((int32_t)*ent < 0) {
                break;
            }
        } while (*ent & SEGMAP_VALID);

        /* 0x00E031B8..0x00E031DA: real frames are reclaimed */
        if (ppn_array[0] >= 0x200 && ppn_array[0] <= 0xFFF) {
            MMAP_$RECLAIM(ppn_array, touched, (flags & 0x20) ? -1 : 0);
        }

        /* 0x00E031E0..0x00E031F2 */
        AST_$WS_FLT_CNT += touched;
        aote->flags |= AOTE_FLAG_TOUCHED;
        return touched;                             /* 0x00E03538 */
    }

    /* 0x00E031F6 */
    kind = 8;

    /* 0x00E031FC..0x00E03202: btst.l #6 on the high word = bit 22 */
    if (*entry & 0x00400000u) {
        /* 0x00E03204..0x00E03224: the run of marked entries */
        run = 0;
        ent = entry;
        do {
            *ent |= SEGMAP_IN_TRANS;
            ent++;
            run++;
            if (run >= avail) {
                break;
            }
            if ((int32_t)*ent < 0) {
                break;
            }
            if (*ent & SEGMAP_VALID) {
                break;
            }
        } while (*ent & 0x00400000u);

        /* 0x00E03226..0x00E0323E: the nested procedure reads flags,
         * ppn_array and status uplevel; passed explicitly */
        touched = (uint16_t)ast_$count_valid_pages(entry, (int16_t)run, flags,
                                                   ppn_array, status);
        aote->flags |= AOTE_FLAG_TOUCHED;
        goto release_rest;                          /* 0x00E033D8 */
    }

    /* 0x00E03242..0x00E0326A: page number within the object vs its
     * last page */
    page_no = ((uint32_t)aste->segment << 5) + (uint32_t)page;
    if (aote->length != 0 &&
        (last_page = (aote->length - 1) >> 10, last_page >= page_no)) {
        /* 0x00E032A2..0x00E032B2: clip to the pages left in the object
         * (signed compare) */
        in_file = last_page - page_no + 1;
        if ((int32_t)(uint32_t)avail > (int32_t)in_file) {
            avail = (uint16_t)in_file;
        }
    } else {
        /* 0x00E0326C..0x00E032A0: beyond the end */
        if ((flags & 0x01) == 0) {
            *status = status_$ast_eof;                          /* 0x30001 */
            return 0;
        }
        if ((flags & 0x02) == 0) {
            if (count == 0x20) {
                if (avail > AST_$GROW_AHEAD_CNT) {
                    avail = AST_$GROW_AHEAD_CNT;
                }
            } else {
                avail = 1;
            }
        }
    }

    /* 0x00E032B4..0x00E032C2: a local entry with no disk address */
    if ((*entry & 0x7FFFFF) == 0 && aote->remote_flag >= 0) {
        /* 0x00E032C4..0x00E032E8: the run of empty entries */
        run = 0;
        ent = entry;
        do {
            *ent |= SEGMAP_IN_TRANS;
            ent++;
            run++;
            if (run >= avail) {
                break;
            }
            if ((int32_t)*ent < 0) {
                break;
            }
            if (*ent & SEGMAP_VALID) {
                break;
            }
        } while ((*ent & 0x7FFFFF) == 0);

        /* 0x00E032EA..0x00E03320: blocks first, then count them in */
        ast_$setup_page_read(aste, entry, page, run, flags, status);
        touched = 0;
        if (*status == status_$ok) {
            touched = (uint16_t)ast_$count_valid_pages(entry, (int16_t)run,
                                                       flags, ppn_array,
                                                       status);
        }
        goto release_rest;
    }

    /* 0x00E03324..0x00E0335A: a run to read; a local run stops at an
     * entry with no disk address, a remote run does not */
    kind = 2;
    run = 0;
    ent = entry;
    do {
        *ent |= SEGMAP_IN_TRANS;
        ent++;
        run++;
        if (run >= avail) {
            break;
        }
        if ((int32_t)*ent < 0) {
            break;
        }
        if (*ent & SEGMAP_VALID) {
            break;
        }
        if (*ent & 0x00400000u) {
            break;
        }
        /* 0x00E0334C tst.b (0xb9,A1) / bmi.b 0x00e0332e: a remote object
         * loops back without looking at the disk address */
    } while (aote->remote_flag < 0 || (*ent & 0x3FFFFF) != 0);

    /* 0x00E0335C..0x00E033AA */
    if (aote->remote_flag < 0) {
        touched = (uint16_t)ast_$read_area_pages_network(
            aste, entry, ppn_array, page, run, (flags & 0x01) ? 0xFF : 0x00,
            status);
    } else {
        aote->flags |= AOTE_FLAG_TOUCHED;
        touched = (uint16_t)ast_$read_area_pages(aste, entry, ppn_array, page,
                                                 run, status);
    }

    /* 0x00E033AC..0x00E033D4: PROC1_$STATS entry pid: +0x04 (flags bit 3)
     * or +0x00 */
    if (flags & 0x08) {
        PROC_STATS_BASE[PROC1_$CURRENT * 4 + 1] += touched;
    } else {
        PROC_STATS_BASE[PROC1_$CURRENT * 4 + 0] += touched;
    }

release_rest:
    /* 0x00E033D8..0x00E033F2: entries left in transition are released */
    if (touched < run) {
        ast_$clear_transition_bits(
            (uint32_t *)((char *)row + (uint16_t)((touched + page) << 2) - 0x80),
            (uint16_t)(run - touched));
    }

    /* 0x00E033F4..0x00E033F6 */
    if (touched == 0) {
        *status |= (status_$t)0x80000000u;          /* 0x00E03530 */
        return touched;
    }

    /* 0x00E033FA..0x00E03400 */
    if (*status == status_$ok) {
        /* 0x00E03404..0x00E034B0: install each page */
        ent = entry;
        for (i = 0; i < (int16_t)touched; i++) {
            ppn = ppn_array[i];
            if (ppn == 0) {
                CRASH_SYSTEM(&pmap_$mismatch_00e00eac);
            }
            mmape = &MMAPE_BASE[ppn];
            if ((int8_t)mmape->flags1 < 0) {
                CRASH_SYSTEM(&mmap_$bad_install_00e03544);
            }
            mmape->wire_count = 0;
            mmape->flags2 &= (uint8_t)~MMAPE_FLAG2_MODIFIED;    /* +0x09 bit 6 */
            mmape->flags1 &= (uint8_t)~MMAPE_FLAG1_IMPURE;      /* +0x05 bit 6 */
            if (flags & 0x08) {
                mmape->flags1 |= MMAPE_FLAG1_IMPURE;
            }
            mmape->flags2 &= (uint8_t)~MMAPE_FLAG2_ON_DISK;     /* +0x09 bit 7 */
            mmape->seg_offset = (uint8_t)(page + i);            /* byte add */
            mmape->segment = aste->seg_index;
            mmape->disk_addr = *ent & 0x7FFFFF;

            /* 0x00E03482..0x00E034AC: frame, PFT, bits 29 and 30, not 31 */
            *ent = (*ent & 0xFFFF0000u) | (ppn & 0xFFFF);
            pft = PFT_FOR_PPN(ppn);
            *pft = (*pft & 0xFFFFBFFFu) | 0x00002000u;
            *ent |= SEGMAP_WIRED;
            *ent |= SEGMAP_VALID;
            *ent &= ~SEGMAP_IN_TRANS;
            ent++;
        }

        /* 0x00E034B4..0x00E034DE */
        MMAP_$INSTALL_LIST(ppn_array, touched, (flags & 0x20) ? -1 : 0);
        aste->page_count = (uint8_t)(aste->page_count + (uint8_t)touched);
        EC_$ADVANCE(&AST_$PMAP_IN_TRANS_EC);
    }

    /* 0x00E034E0..0x00E034E4 */
    AST_$PAGE_FLT_CNT += touched;

    /* 0x00E034E8..0x00E0352E: kind, uid, segment, page, first frame's low
     * word, count, remote (1/0), 0 */
    if (NETLOG_$OK_TO_LOG < 0) {
        NETLOG_$LOG_IT(kind, (uint32_t *)&aote->uid, aste->segment, page,
                       (uint16_t)(ppn_array[0] & 0xFFFF), touched,
                       (aote->remote_flag < 0) ? 1 : 0, 0);
    }

    /* 0x00E03538 */
    return touched;
}
