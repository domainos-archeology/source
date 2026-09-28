/*
 * MST_$ALLOC_ASID - Allocate a new Address Space ID
 *
 * Original address: 0x00E42D3A
 * Size: 258 bytes (0x00E42D3A .. 0x00E42E3B)
 *
 * Under ML lock 0x0C the routine scans the 58-entry ASID set for a clear
 * bit, marks the first one found with MST_$SET, makes sure the ASID's first
 * MST page exists (MST_$ALLOC_TABLE_PAGE) and then makes sure the first
 * segment-table entry of that page carries OS_WIRED_$UID: an entry that
 * already holds it is left alone, an empty one is initialised, and an entry
 * holding any other UID is a "no space" failure.
 *
 * Frame (link.w A6,-0x10; D2-D4/A2 saved):
 *   (0x8,A6)   status_ret   pointer to the status_$t result
 *   (-0x4,A6)  status       the status being built
 *   D2w        asid         the candidate / result ASID
 *
 * Re-emitted from the disassembly 2026-09-19: the previous C located the
 * MSTE page at MSTE_PAGES + page * 0x400, but the image forms
 * `lea (0x0,A0,D1*0x1),A0` from A0 = 0xEF6400 and then addresses every
 * field as `(-0x400,A0)`, `(-0x3fc,A0)`, `(-0x3f8,A0)`, `(-0x3f6,A0)`
 * (0x00E42DDA .. 0x00E42E08) - MST holds a ONE-based page number, exactly
 * as mst_$va_to_pte documents, so the entry lives one page lower.
 */

#include "mst/mst_internal.h"

/*
 * `movea.l #0xef6400,A0` at 0x00E42DC0: the base of the MSTE pages (SAU2 map
 * symbol MSTE_PAGES at EF6400).  The entry for one-based page P is at
 * MSTE_PAGES + P * 0x400 - 0x400.
 */
#define MST_ALLOC_ASID_MSTE_PAGES MST_PAGE_TABLE_BASE

/*
 * @param status_ret  Output: status code (status_$ok on success)
 * @return The allocated ASID in D0w, or 0 when the status is not status_$ok
 *         (0x00E42E2A .. 0x00E42E30)
 */
uint16_t MST_$ALLOC_ASID(status_$t *status_ret)
{
    status_$t status;        /* (-0x4,A6) */
    uint16_t asid;           /* D2w */
    int16_t probe;           /* D0w, the dbf counter */
    uint16_t byte_index;     /* D1w */
    uint16_t table_base;     /* D4w / D3w: MST_ASID_BASE[asid] */
    uint32_t page;           /* D1: MST[table_base], zero-extended */
    mst_entry_t *entry;      /* (-0x400,A0) */

    /* 0x00E42D42 .. 0x00E42D4E: ML_$LOCK(0xC) with a Pascal result slot */
    ML_$LOCK(MST_LOCK_ASID);

    /*
     * 0x00E42D50 .. 0x00E42D6C: `moveq #0x39,D0` / `dbf` = 58 probes.
     * ASID N is bit (N & 7) of byte (0x3f - N) >> 3 of MST_$ASID_LIST
     * (`moveq #0x3f,D1` / `sub.w D2w,D1w` / `lsr.w #0x3,D1w` /
     * `btst.b D2,(0x0,A2,D1w*0x1)`).
     */
    asid = 0;
    for (probe = 0x39; probe != -1; probe--) {
        byte_index = (uint16_t)(0x3f - asid) >> 3;
        if ((MST_$ASID_LIST[byte_index] & (uint8_t)(1u << (asid & 7))) == 0) {
            goto found;
        }
        asid++;
    }

    /* 0x00E42D70: we didn't find one */
    status = status_$no_asid_available;
    goto done;

found:
    /* 0x00E42D7C .. 0x00E42D8A: MST_$SET(&MST_$ASID_LIST, 0x3a, asid) */
    MST_$SET(MST_$ASID_LIST, MST_MAX_ASIDS, asid);

    /*
     * 0x00E42D8C .. 0x00E42DB0: table_base = MST_ASID_BASE[asid] (word, so
     * the byte offset into MST is table_base * 2, formed with `add.w D3w,D3w`
     * and used as a sign-extended word index);
     * MST_$ALLOC_TABLE_PAGE(asid, 0, &MST[table_base]).
     */
    table_base = MST_ASID_BASE[asid];
    status = MST_$ALLOC_TABLE_PAGE(asid, 0, &MST[(int16_t)(table_base * 2) / 2]);
    if (status != status_$ok) {
        goto done;      /* 0x00E42DB6 bne.b */
    }

    /*
     * 0x00E42DB8 .. 0x00E42DD4: page = MST[table_base] zero-extended
     * (`clr.l D1` / `move.w`), `lsl.l #0x8` + `lsl.l #0x2` = * 0x400,
     * A0 = MSTE_PAGES + page * 0x400; the entry itself is at (-0x400,A0).
     */
    page = MST[(int16_t)(table_base * 2) / 2];
    entry = (mst_entry_t *)ARCH_VA_TO_PTR(MST_ALLOC_ASID_MSTE_PAGES + (page << 10) - 0x400);

    /*
     * 0x00E42DCA .. 0x00E42DE4: `cmpm.l (A2)+,(A1)+` twice against
     * OS_WIRED_$UID (0xE1740C).  Both longwords equal -> status_$ok.
     */
    if (entry->uid.high != OS_WIRED_$UID.high || entry->uid.low != OS_WIRED_$UID.low) {
        /* 0x00E42DE6 .. 0x00E42DF4: `tst.l (-0x400,A0)` - any other UID */
        if (entry->uid.high != 0) {
            status = status_$no_space_available;
            goto done;
        }

        /*
         * 0x00E42DF6 .. 0x00E42E08: uid = OS_WIRED_$UID, `clr.w (-0x3f8,A0)`
         * area_id = 0, `andi.w #0x3e00,(-0x3f6,A0)` on the flags word.
         */
        entry->uid.high = OS_WIRED_$UID.high;
        entry->uid.low = OS_WIRED_$UID.low;
        entry->area_id = 0;
        entry->flags &= 0x3e00;
    }

    /* 0x00E42E0E clr.l (-0x4,A6) */
    status = status_$ok;

done:
    /* 0x00E42E12 .. 0x00E42E1E: ML_$UNLOCK(0xC) */
    ML_$UNLOCK(MST_LOCK_ASID);

    /* 0x00E42E20 .. 0x00E42E30: *status_ret = status; D0w = ok ? asid : 0 */
    *status_ret = status;
    if (status != status_$ok) {
        return 0;
    }
    return asid;
}
