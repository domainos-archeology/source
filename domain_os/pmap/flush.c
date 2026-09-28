/*
 * PMAP_$FLUSH - Write a segment's modified pages back
 *
 * 0x00E1376C - 0x00E13A12 (680 bytes, A5 = 0xE24D44).  Re-emitted from the
 * disassembly 2026-09-27.  Wrong before: the segment-map entries were read
 * through a `uint16_t *' (the high word on the target, the low word on a
 * little-endian host), the INSTALLED bit was cleared through a byte cast,
 * and the batch array had 16 slots for the 1-based indexes 1..16 the image
 * uses ((-0x44,A6) + n * 4, 0x00E13944).
 *
 * Arguments:
 *   (0x8,A6)  aste        -> aste_t: page_count (+0x10) keeps the outer
 *                          loop going, flags (+0x12) bits 11 and 12 are
 *                          sampled at entry
 *   (0xc,A6)  segmap      -> the segment's 32 longword entries
 *   (0x10,A6) start_page  word (D6 counts up from it)
 *   (0x12,A6) count       word (D4 = count - 1, `dbf')
 *   (0x14,A6) flags       word, read as the byte (0x15,A6): bit 0 = drop
 *                          the MMU mapping, bit 1 = do not write, bit 2 =
 *                          asynchronous write
 *   (0x16,A6) status      -> status_$t, cleared at entry
 * Result: D2, the number of pages written.
 *
 * The two helpers are nested procedures reached through the frame:
 * pmap_$flush_write_batch (0x00E1360C, no arguments) and
 * pmap_$update_seg_map (0x00E1359C, static link in A1); both were
 * flattened with explicit uplevel parameters earlier.
 */

#include "pmap/pmap_internal.h"
#include "ast/ast.h"
#include "misc/misc.h"
#include "mmap/mmap.h"
#include "mmu/mmu.h"
#include "time/time.h"

#define PMAP_FLUSH_BATCH_MAX    0x10    /* 0x00E13948 */
#define PMAP_FLUSH_FLAG_UNMAP   0x01    /* btst.b #0,(0x15,A6) */
#define PMAP_FLUSH_FLAG_NOWRITE 0x02    /* btst.b #1,(0x15,A6) */
#define PMAP_FLUSH_FLAG_ASYNC   0x04    /* btst.b #2,(0x15,A6) */
#define PMAP_FLUSH_VPN_LOW      0x200   /* 0x00E13816 / 0x00E1383A */
#define PMAP_FLUSH_VPN_HIGH     0xFFF   /* 0x00E1381E / 0x00E13842 */
#define ASTE_FLAG_BIT11         0x0800  /* btst.l #0xb at 0x00E137A4 */
#define ASTE_FLAG_BIT12         0x1000  /* btst.l #0xc at 0x00E13792 */
#define MMAPE_FLAG2_DIRTY       0x40    /* bit 6 of mmape_t.flags2 */
#define AOTE_FLAG_BIT5          0x20    /* bset.b #5,(0xbf,A2) */

/* 0x00E13872: `pea (0x1a0,PC)` -> the cell at 0x00E13A14, 00 05 00 03 */

int16_t PMAP_$FLUSH(struct aste_t *aste, uint32_t *segmap, uint16_t start_page,
                    int16_t count, uint16_t flags, status_$t *status)
{
    uint32_t batch_vpns[PMAP_FLUSH_BATCH_MAX + 1];  /* (-0x44,A6), 1-based */
    int16_t batch_count;            /* (-0x50,A6) */
    int16_t written;                /* D2 */
    boolean has_bit12;              /* (-0x56,A6) */
    boolean has_bit11;              /* D3 */
    boolean any_modified;           /* (-0x5E,A6) */
    boolean any_invalid;            /* (-0x60,A6) */
    boolean wrote_direct;           /* (-0x5A,A6) */
    boolean was_modified;           /* (-0x58,A6) */
    int16_t n;                      /* D4 */
    uint16_t page_idx;              /* D6 */
    uint32_t *e;                    /* (-0x78,A6) / A2 */
    uint32_t entry;
    uint32_t vpn;                   /* D5 */
    mmape_t *page;                  /* A3 */
    uint32_t *pft;                  /* A0 */
    aote_t *aote;
    int16_t i;

    /* 0x00E1377A - 0x00E137AA */
    *status = status_$ok;
    batch_count = 0;
    written = 0;
    has_bit12 = (aste->flags & ASTE_FLAG_BIT12) ? true : false;
    has_bit11 = (aste->flags & ASTE_FLAG_BIT11) ? true : false;
    any_modified = false;

    /* 0x00E137AE - 0x00E137C4 */
    ML_$LOCK(PMAP_LOCK_ID);
    n = (int16_t)((int16_t)(start_page + count - 1) - (int16_t)start_page);

    /* 0x00E139AE: outer loop while the ASTE still has pages */
    while (aste->page_count != 0) {
        /* 0x00E137CC - 0x00E137D6 */
        any_invalid = false;
        wrote_direct = false;
        if (n >= 0) {
            page_idx = start_page;
            e = segmap + start_page;
            for (i = 0; i <= n; i++, page_idx++, e++) {
                entry = *e;
                /* 0x00E137F6: `tst.w (A2)` - bit 31 marks an entry in use */
                if ((int32_t)entry < 0) {
                    any_invalid = true;
                    continue;
                }
                /* 0x00E13802 */
                if ((entry & PMAP_SEGMAP_L_VALID) == 0) {
                    continue;
                }
                vpn = entry & PMAP_SEGMAP_L_VPN_MASK;
                /* 0x00E1380C - 0x00E13822: unless shutting down, only
                 * pageable VPNs are considered */
                if (PMAP_$SHUTTING_DOWN_FLAG >= 0 &&
                    (vpn < PMAP_FLUSH_VPN_LOW || vpn > PMAP_FLUSH_VPN_HIGH)) {
                    continue;
                }
                page = MMAPE_FOR_VPN(vpn);
                /* 0x00E1383A - 0x00E13864: a non-pageable or wired page is
                 * a "pages wired" failure */
                if (vpn < PMAP_FLUSH_VPN_LOW || vpn > PMAP_FLUSH_VPN_HIGH ||
                    page->wire_count != 0) {
                    if (batch_count > 0) {
                        pmap_$flush_write_batch(&batch_count, batch_vpns, segmap,
                                                status, aste, flags);
                    }
                    *status = status_$pmap_pages_wired;
                    goto done;
                }
                /* 0x00E13868 - 0x00E1387C */
                if (page->seg_offset != (uint8_t)page_idx) {
                    CRASH_SYSTEM(&status_$t_00e13a14);
                }
                /* 0x00E1387E: the second argument is `st -(SP)' */
                MMAP_$UNAVAIL_REMOV(vpn, true);
                /* 0x00E1388C - 0x00E138A8 */
                if ((flags & PMAP_FLUSH_FLAG_UNMAP) != 0 &&
                    (*e & PMAP_SEGMAP_L_INSTALLED) != 0) {
                    *e &= ~PMAP_SEGMAP_L_INSTALLED;
                    MMU_$REMOVE(vpn);
                }
                /* 0x00E138AA - 0x00E138C8 */
                pft = PFT_FOR_PPN(vpn);
                if ((*pft & PFT_FLAG_MODIFIED) == 0 &&
                    (page->flags2 & MMAPE_FLAG2_DIRTY) == 0) {
                    was_modified = false;                       /* 0x00E1395E */
                } else {
                    /* 0x00E138CC - 0x00E138F0 */
                    if ((*pft & PFT_FLAG_MODIFIED) != 0) {
                        any_modified = true;
                    }
                    *pft &= ~(uint32_t)PFT_FLAG_MODIFIED;
                    was_modified = true;
                    page->flags2 &= (uint8_t)~MMAPE_FLAG2_DIRTY;
                    if ((flags & PMAP_FLUSH_FLAG_NOWRITE) == 0) {
                        /* 0x00E138F2 - 0x00E138F6 */
                        wrote_direct = true;
                        written++;
                        if (has_bit11 < 0) {
                            /* 0x00E138FC - 0x00E13932: write it now; the
                             * sync flag is `seq' of flags bit 2 */
                            pmap_$write_page(vpn, status,
                                             (flags & PMAP_FLUSH_FLAG_ASYNC) == 0 ? true : false);
                            if (*status != status_$ok) {
                                goto done;
                            }
                            pmap_$update_seg_map(aste, flags, e, vpn, page_idx);
                        } else {
                            /* 0x00E13934 - 0x00E1395C: batch it */
                            *e |= PMAP_SEGMAP_L_WRITING;
                            batch_count++;
                            batch_vpns[batch_count] = vpn;
                            if (batch_count == PMAP_FLUSH_BATCH_MAX) {
                                pmap_$flush_write_batch(&batch_count, batch_vpns, segmap,
                                                        status, aste, flags);
                                if (*status != status_$ok) {
                                    goto done;
                                }
                            }
                        }
                    }
                }
                /* 0x00E13962 - 0x00E1397E */
                if (was_modified >= 0 || (flags & PMAP_FLUSH_FLAG_NOWRITE) != 0) {
                    pmap_$update_seg_map(aste, flags, e, vpn, page_idx);
                }
            }
        }
        /* 0x00E1398C - 0x00E1399C */
        if (batch_count > 0) {
            pmap_$flush_write_batch(&batch_count, batch_vpns, segmap,
                                    status, aste, flags);
            if (*status != status_$ok) {
                break;
            }
        }
        /* 0x00E1399E - 0x00E139AA */
        if (any_invalid >= 0) {
            break;
        }
        if (wrote_direct >= 0) {
            pmap_$wait_in_transit();
        }
    }

done:
    /* 0x00E139BA - 0x00E139F6: stamp the object's clocks */
    if (any_modified < 0 && has_bit11 >= 0 && has_bit12 >= 0) {
        aote = aste->aote;
        TIME_$ABS_CLOCK((clock_t *)&aote->dtv_high);
        TIME_$CLOCK((clock_t *)&aote->dtm_high);
        aote->dta_high = aote->dtm_high;
        aote->dta_low = aote->dtm_low;
        aote->flags |= AOTE_FLAG_BIT5;
    }
    /* 0x00E139FC - 0x00E13A08 */
    ML_$UNLOCK(PMAP_LOCK_ID);
    return written;
}
