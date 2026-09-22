/*
 * DBUF_$UPDATE_VOL - Write every idle dirty buffer of a volume back
 *
 * 0x00E3AAA2 - 0x00E3ABD8 (312 bytes, A5 = 0xE78B58).  Re-emitted from the
 * disassembly on 2026-09-19.  Wrong before: the dirty tests were big-endian
 * word casts and the DISK_$WRITE header had the type byte at +0x0C instead
 * of +0x10.  The second argument is never read (the frame only loads
 * (0x8,A6)).
 *
 * Arguments:
 *   (0x8,A6) vol_idx  word (D2); 0 means every volume
 *   (0xc,A6) uid_p    not read
 *
 * For each of DBUF[0 .. dbuf_$count-1] (A3 = &entry.block, stride 0x24):
 * skip unless dirty and (vol_idx == 0 or the entry's volume matches);
 * then under the spin lock re-check the volume, dirty, ref_count == 0 and
 * not busy, mark busy, drop the lock, write the block back (a failure
 * sets the volume's DBUF_$TROUBLE bit; the local status is otherwise
 * unused), re-lock, clear busy, EC_$ADVANCE if anyone waits (with the
 * lock still held, 0x00E3ABAC), and unlock.
 */

#include "dbuf/dbuf_internal.h"

void DBUF_$UPDATE_VOL(uint16_t vol_idx, void *uid_p)
{
    ml_$spin_token_t token;         /* (-0x26,A6) */
    status_$t local_status;         /* (-0x24,A6) */
    uint32_t header[8] = {0};       /* (-0x20,A6) */
    int16_t n;                      /* D3 */
    dbuf_$entry_t *e;               /* A3 */
    int8_t ok;                      /* D1b at 0x00E3AAF4 - 0x00E3AB02 */
    uint16_t i;

    (void)uid_p;

    /* 0x00E3AAB4 - 0x00E3AABE */
    n = (int16_t)(dbuf_$count - 1);
    if (n < 0) {
        return;
    }

    for (i = 0; i <= (uint16_t)n; i++) {
        e = &DBUF[i];

        /* 0x00E3AAC6 - 0x00E3AADE */
        if ((e->flags & DBUF_ENTRY_DIRTY) == 0) {
            continue;
        }
        if (vol_idx != 0 && DBUF_GET_VOL(e) != vol_idx) {
            continue;
        }

        /* 0x00E3AAE2 - 0x00E3AB1E: re-check under the lock */
        token = ML_$SPIN_LOCK(&DBUF_SPIN_LOCK);
        ok = (int8_t)((vol_idx == 0 ? -1 : 0) | (DBUF_GET_VOL(e) == vol_idx ? -1 : 0));
        if (ok >= 0 ||
            (e->flags & DBUF_ENTRY_DIRTY) == 0 ||
            e->ref_count != 0 ||
            (e->flags & DBUF_ENTRY_BUSY) != 0) {
            ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);              /* 0x00E3ABB6 */
            continue;
        }

        /* 0x00E3AB22 - 0x00E3AB8C */
        e->flags |= DBUF_ENTRY_BUSY;
        ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
        dbuf_$fill_write_header(e, header);
        e->flags &= (uint8_t)~DBUF_ENTRY_DIRTY;
        DISK_$WRITE((int16_t)DBUF_GET_VOL(e), (uint32_t)e->block, e->ppn,
                    header, &local_status);
        if (local_status != status_$ok) {
            DBUF_$TROUBLE |= (uint16_t)(1u << DBUF_GET_VOL(e));
        }

        /* 0x00E3AB90 - 0x00E3ABC6 */
        token = ML_$SPIN_LOCK(&DBUF_SPIN_LOCK);
        e->flags &= (uint8_t)~DBUF_ENTRY_BUSY;
        if (dbuf_$waiters != 0) {
            EC_$ADVANCE(&dbuf_$eventcount);
        }
        ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
    }
}
