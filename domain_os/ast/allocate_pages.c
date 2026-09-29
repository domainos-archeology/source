/*
 * ast_$allocate_pages - Allocate physical pages for segment mapping
 *
 * Allocates pages from the free pool and pure pool. Updates page
 * attributes and wakes the purifier if memory is low.
 *
 * Parameters:
 *   count     - Number of pages requested (0x00E00D4E: move.w (0x8,A6),D2w)
 *   min_count - Minimum that must be obtained before the retry loop gives
 *               up on waking the purifier (0x00E00E56: cmp.w (0xa,A6),D0w).
 *               Every caller in the image passes 1.
 *   ppn_array - Array to receive allocated page numbers
 *               (0x00E00D52: movea.l (0xc,A6),A4)
 *
 * Returns: Number of pages actually allocated
 *
 * Original address: 0x00e00d46 .. 0x00E00EA8 (356 bytes)
 */

#include "ast/ast_internal.h"

/*
 * Status cells passed to CRASH_SYSTEM by `pea (d,PC)`.
 *
 * These are constant longwords in this module's own code region, not
 * shared globals; the cell address is part of each name.  Names come from
 * the SR10.4 status-code database.
 */
/*
 * 0x00E00DF6: pea (0xb4,PC) -> 0x00E00EAC, jsr CRASH_SYSTEM at 0x00E00DFA.
 * Shared with AST_$PMAP_ASSOC, AST_$ASSOC_AREA and AST_$TOUCH.
 */
static const status_$t pmap_$mismatch_00e00eac = 0x00050003;

/*
 * ast_$allocate_pages_log - the nested NETLOG helper at 0x00E00CAC
 *                           (154 bytes)
 *
 * The SAU2 link map gives 0x00E00CAC no symbol (AST_ runs from 0xE00BE8 and
 * its first named entry is AST_$WAIT_FOR_AST_INTRANS at 0xE00C54), so this
 * keeps a lowercase module-local name rather than being spelled
 * NETLOG_$LOG_PAGE.  (source-0oq1)
 *
 * Prototype below; the body follows ast_$allocate_pages.
 */
static void ast_$allocate_pages_log(const mmape_t *pmape, uint16_t seg,
                                    uint32_t ppn, uint16_t allocated,
                                    uint16_t min_count);

/*
 * Allocation stats at A5+0x460 and A5+0x464: the map's
 * AST_$ALLOC_TOO_FEW_CNT and AST_$ALLOC_CNT (AST_$DATA fields).
 */

int16_t ast_$allocate_pages(int16_t count_arg, int16_t min_count,
                            uint32_t *ppn_array)
{
    uint16_t num_pages;
    uint16_t allocated;
    uint16_t count;
    int16_t i;
    uint32_t ppn;
    uint16_t seg_index;

    /* 0x00E00D56 */
    AST_$ALLOC_CNT++;

    /* 0x00E00D5A / 0x00E00D4E */
    allocated = 0;
    num_pages = (uint16_t)count_arg;

    while (1) {
        /* 0x00E00D6C-0x00E00D8E: first the free pool */
        count = MMAP_$ALLOC_FREE(ppn_array, num_pages);
        if (count != 0) {
            allocated += count;
            num_pages -= count;
            if (num_pages == 0) {
                goto done;
            }
            ppn_array += count;
        }

        /* 0x00E00D90-0x00E00DA2: then the pure pool */
        count = MMAP_$ALLOC_PURE(ppn_array, num_pages);
        num_pages -= count;

        if (count != 0) {
            /* 0x00E00DA6-0x00E00DA8 plus the dbf: `count` passes */
            i = (int16_t)(count - 1);
            do {
                mmape_t *pmape;
                uint32_t *segmap_entry;

                /*
                 * 0x00E00DAC-0x00E00DB4.  A3 is a BIASED cursor:
                 * 0xEB4800 + ppn*0x10, and every field is read at
                 * -0x2000 from it, i.e. MMAPE_FOR_VPN(ppn) = 0xEB2800 + ppn*0x10.
                 */
                ppn = *ppn_array;
                pmape = MMAPE_FOR_VPN(ppn);

                /* 0x00E00DBE: the owning segment index */
                seg_index = pmape->segment;

                /*
                 * 0x00E00DB8-0x00E00DD2 then `(-0x80,A2)`: A2 is 0xED5000 +
                 * seg*0x80 + seg_offset*4 and the displacement takes it
                 * 0x80 down, onto entry seg_offset of segment seg's row.
                 */
                segmap_entry = (uint32_t *)((char *)PMAP_SEGMAP_ROW(seg_index)
                                            + (uint32_t)pmape->seg_offset * 4);

                /*
                 * 0x00E00DD6-0x00E00DF4.  The PPN is the entry's low word
                 * compared as a longword, and the three flag tests are
                 * `tst.w`/`bmi` (bit 31), `btst.l #0xe` (bit 30) and
                 * `btst.l #0xd` (bit 29) on the entry's high word.
                 */
                if ((uint32_t)(uint16_t)*segmap_entry != ppn ||
                    (*segmap_entry & SEGMAP_FLAG_IN_TRANS) != 0 ||
                    (*segmap_entry & SEGMAP_FLAG_IN_USE) == 0 ||
                    (*segmap_entry & SEGMAP_FLAG_INSTALLED) != 0) {
                    CRASH_SYSTEM(&pmap_$mismatch_00e00eac);
                }

                /*
                 * 0x00E00E02 `bclr.b #0x6,(-0x80,A2)`: bit 6 of the entry's
                 * FIRST byte is bit 30 of the longword - the same bit the
                 * `btst.l #0xe` above tested.  Expressed as a longword mask
                 * so it lands on the right bit on either endianness.
                 */
                *segmap_entry &= ~(uint32_t)SEGMAP_FLAG_IN_USE;

                /* 0x00E00E08: keep only bits 31..23 */
                *segmap_entry &= 0xFF800000u;

                /* 0x00E00E10-0x00E00E14: fold in the saved disk address */
                *segmap_entry |= pmape->disk_addr;

                /*
                 * 0x00E00E18-0x00E00E2C: `subq.b #0x1,(-0x4,A0,D0*0x1)`
                 * with A0 = 0xEC5400 and D0 = seg*0x14, i.e. the
                 * page_count byte (+0x10) of the 1-based AST_ASTE_ENTRY(seg).
                 */
                AST_ASTE_ENTRY(seg_index)->page_count--;

                /* 0x00E00E30-0x00E00E44 */
                if (NETLOG_$OK_TO_LOG < 0) {
                    ast_$allocate_pages_log(pmape, seg_index, ppn,
                                            allocated, (uint16_t)min_count);
                }

                /* 0x00E00E48-0x00E00E4E */
                allocated++;
                ppn_array++;
                i--;
            } while (i != -1);
        }

        /* 0x00E00E52-0x00E00E5A: unsigned word compare */
        if (allocated >= (uint16_t)min_count) {
            break;
        }

        /* 0x00E00E5C-0x00E00E68 */
        PMAP_$WAKE_PURIFIER(0xFF);
    }

    /* 0x00E00E6C-0x00E00E70 */
    if (num_pages != 0) {
        AST_$ALLOC_TOO_FEW_CNT++;
    }

done:
    /* 0x00E00E74-0x00E00E9A */
    if ((MMAP_$WSL_FREE_CNT + MMAP_$WSL_PURE_CNT + MMAP_$WSL_IMPURE_CNT)
        < (uint32_t)PMAP_$DATA.low_thresh) {
        PMAP_$WAKE_PURIFIER(0);
    }

    /* 0x00E00E9C */
    return (int16_t)allocated;
}

/*
 * ast_$allocate_pages_log - log one page reclaimed from the pure pool
 *
 * Three stack arguments of its own (0x00E00E36-0x00E00E44 pushes a two-byte
 * result slot, `move.l (A4),-(SP)` = ppn, `move.w D4w,-(SP)` = seg and
 * `pea (-0x2000,A3)` = pmape, then `lea (0xc,SP),SP`):
 *
 *   A6+0x08 long  pmape   the mmape_t for the page being released
 *   A6+0x0C word  seg     the segment index - pushed but NEVER read; the
 *                         body takes the index from pmape->segment instead
 *                         (0x00E00CBC `move.w (0x2,A2),D0w`)
 *   A6+0x0E long  ppn     the physical page number (D2)
 *
 * and two more values read UPLEVEL out of ast_$allocate_pages' frame through
 * the static link (0x00E00CC0 `movea.l (A6),A3`, so A3 is the parent's frame
 * pointer):
 *
 *   (-0xe,A3)  the parent's `allocated` counter
 *   (0xa,A3)   the parent's `min_count` argument
 *
 * Both are passed explicitly here.  The ASTE cursor A4 is
 * 0xEC5400 + seg*0x14 (0x00E00CC4-0x00E00CD0), so (-0x10,A4) is the `aote`
 * field (+0x04) and (-0x8,A4) the `timestamp` field (+0x0C) of the 1-based
 * AST_ASTE_ENTRY(seg).
 *
 * Original address: 0x00E00CAC .. 0x00E00D44
 */
static void ast_$allocate_pages_log(const mmape_t *pmape, uint16_t seg,
                                    uint32_t ppn, uint16_t allocated,
                                    uint16_t min_count)
{
    const aste_t *aste;
    uint32_t log_uid[2];
    const uint32_t *uid_ptr;

    (void)seg;

    /* 0x00E00CBC-0x00E00CD0 */
    aste = AST_ASTE_ENTRY(pmape->segment);

    /* 0x00E00CD4: `tst.b (0x9,A2)` / `bpl` - mmape flags2 bit 7 */
    if ((int8_t)pmape->flags2 < 0) {
        /* 0x00E00CDA: only the FIRST longword of ANON_$UID is copied */
        log_uid[0] = ANON_$UID.high;
        /* 0x00E00CE2-0x00E00CEC: a zero-extended word from aote+0x2A */
        log_uid[1] = (uint32_t)(uint16_t)(aste->aote->dtm_high & 0xFFFF);
        uid_ptr = log_uid;
    } else {
        /* 0x00E00D2A-0x00E00D2E: `pea (0x10,A0)` - the AOTE's own UID */
        uid_ptr = (const uint32_t *)&aste->aote->uid;
    }

    /*
     * 0x00E00CF0-0x00E00D36 (the two arms push an identical argument list):
     *   kind   #4
     *   uid    the eight bytes selected above
     *   param3 (-0x8,A4)  = aste->segment
     *   param4 (0x1,A2)   = pmape->seg_offset, zero-extended
     *   param5 D2w        = the LOW word of the ppn
     *   param6 (-0xe,A3)  = the parent's `allocated`
     *   param7 (0xa,A3)   = the parent's `min_count`
     *   param8 0
     */
    NETLOG_$LOG_IT(4, (uint32_t *)(uintptr_t)uid_ptr,
                   aste->segment,
                   (uint16_t)pmape->seg_offset,
                   (uint16_t)ppn,
                   allocated,
                   min_count,
                   0);
}
