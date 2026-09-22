/*
 * DBUF_$SET_BUFF - Release and/or write back a buffer from DBUF_$GET_BLOCK
 *
 * 0x00E3A8B6 - 0x00E3A9E2 (302 bytes, A5 = 0xE78B58).  Re-emitted from the
 * disassembly on 2026-09-19.  Wrong before: the dirty test was a
 * big-endian word cast, and the DISK_$WRITE header was a 14-byte record
 * with the type byte at +0x0C (the image builds the eight-longword frame
 * area with the type at +0x10).
 *
 * Arguments:
 *   (0x8,A6) buffer  the buffer VA (D2), matched against entry.data
 *   (0xc,A6) flags   word (D3): DBUF_FLAG_DIRTY / WRITEBACK / INVALIDATE /
 *                    RELEASE, tested in that order
 *   (0xe,A6) status  -> status_$t (A2); cleared once the entry is found
 *
 * The list is searched from the head under the spin lock; no match
 * unlocks and crashes with OS_DBUF_bad_ptr_err (cell 0xE3A9E8,
 * `pea (0x16,PC)` at 0x00E3A9D0).  Every flag is then processed with the
 * lock RELEASED.  RELEASE on a zero ref_count crashes with
 * OS_DBUF_bad_free_err (cell 0xE3A9E4, `pea (0x52,PC)` at 0x00E3A990).
 */

#include "dbuf/dbuf_internal.h"

void DBUF_$SET_BUFF(void *buffer, uint16_t flags, status_$t *status)
{
    ml_$spin_token_t token;         /* (-0x26,A6) */
    uint32_t buffer_va;             /* D2 */
    dbuf_$entry_t *e;               /* A3 */
    uint32_t header[8] = {0};       /* (-0x20,A6) */

    buffer_va = ARCH_PTR_TO_VA(buffer);

    /* 0x00E3A8D0 - 0x00E3A8E8, 0x00E3A9B6 - 0x00E3A9D4 */
    token = ML_$SPIN_LOCK(&DBUF_SPIN_LOCK);
    e = dbuf_$entry_ptr(dbuf_$head);
    while (e->data != buffer_va) {
        e = dbuf_$entry_ptr(e->next);
        if (e == NULL) {
            ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
            CRASH_SYSTEM(&OS_DBUF_bad_ptr_err);
            return;
        }
    }

    /* 0x00E3A8EC - 0x00E3A8FC */
    ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
    *status = status_$ok;

    /* 0x00E3A8FE - 0x00E3A904 */
    if ((flags & DBUF_FLAG_DIRTY) != 0) {
        e->flags |= DBUF_ENTRY_DIRTY;
    }

    /* 0x00E3A90A - 0x00E3A968: write back only if dirty (`btst.l #0xe`
     * on the flags/type word) */
    if ((flags & DBUF_FLAG_WRITEBACK) != 0 && (e->flags & DBUF_ENTRY_DIRTY) != 0) {
        dbuf_$fill_write_header(e, header);
        e->flags &= (uint8_t)~DBUF_ENTRY_DIRTY;
        DISK_$WRITE((int16_t)DBUF_GET_VOL(e), (uint32_t)e->block, e->ppn,
                    header, status);
        if (*status != status_$ok) {
            DBUF_$TROUBLE |= (uint16_t)(1u << DBUF_GET_VOL(e));
        }
    }

    /* 0x00E3A96C - 0x00E3A97E */
    if ((flags & DBUF_FLAG_INVALIDATE) != 0) {
        e->flags &= 0xF0;
        e->block = -1;
        e->flags &= (uint8_t)~DBUF_ENTRY_DIRTY;
    }

    /* 0x00E3A984 - 0x00E3A9B4 */
    if ((flags & DBUF_FLAG_RELEASE) != 0) {
        if (e->ref_count == 0) {
            CRASH_SYSTEM(&OS_DBUF_bad_free_err);
        }
        e->ref_count--;
        if (e->ref_count == 0 && dbuf_$waiters != 0) {
            EC_$ADVANCE(&dbuf_$eventcount);
        }
    }
}
