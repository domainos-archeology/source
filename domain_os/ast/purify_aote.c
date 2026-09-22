/*
 * ast_$purify_aote - Write an object's attributes back
 *
 * Remote object: when the AOTE is TOUCHED and the object is not
 * read-only (attr_flags_lo bit 0), TOUCHED is cleared and the home node
 * is asked for fresh attributes with NETWORK_$AST_GET_INFO (AST lock
 * released around the call); on success the DTU (record +0x24/+0x28) is
 * copied into aote+0x30/+0x34 under the PMAP lock, on failure TOUCHED is
 * put back.  The caller's status is never touched on this path.
 *
 * Local object: TOUCHED stamps the DTU with TIME_$CLOCK and sets DIRTY;
 * DIRTY copies the 0x90-byte attribute block and writes it with
 * VTOCE_$WRITE (AST lock released around the call).  "disk write
 * protected" is swallowed; any other failure sets bit 31 of the status
 * and restores DIRTY.
 *
 * Parameters (frame at 0x00E013A0, `link.w A6,-0x9c`):
 *   aote   (0x8,A6)  (A2)
 *   flags  (0xC,A6)  ONE BYTE (`move.b (0xc,A6),-(SP)` at 0x00E014B8) handed
 *                    to VTOCE_$WRITE unchanged
 *   status (0xE,A6)  (A3), cleared first
 * Locals: (-0x98) the 0x90-byte attribute record, (-0x9C) GET_INFO's status.
 *
 * Original address: 0x00E013A0 (352 bytes).  No A5.
 */

#include "ast/ast_internal.h"

/*
 * NETWORK_$AST_GET_INFO's request-flags word: `pea (0x112,PC)` at
 * 0x00E013EC -> 0x00E01500, image bytes 00 80.  (ast_$force_activate_segment
 * and AST_$LOOKUP_WITH_HINTS use a different cell, 0x00E01D64 = 00 08.)
 */
static const uint16_t ast_$purify_net_info_flags_00e01500 = 0x0080;

void ast_$purify_aote(aote_t *aote, boolean flags, status_$t *status)
{
    status_$t local_status;         /* (-0x9C,A6) */
    uint32_t attrs[36];             /* (-0x98,A6) */
    uint32_t *src;
    uint32_t *dst;
    int16_t i;

    /* 0x00E013B0 */
    *status = status_$ok;

    /* 0x00E013B2 */
    if (aote->remote_flag < 0) {
        /* 0x00E013BA..0x00E013CC: move.w (0xbe,A2) / btst.l #4 = flags
         * bit 4 (TOUCHED); btst.b #0,(0xf,A2) = read-only */
        if ((aote->flags & AOTE_FLAG_TOUCHED) == 0) {
            return;
        }
        if (aote->attr_flags_lo & 0x01) {
            return;
        }
        /* 0x00E013D0..0x00E0140A */
        aote->flags &= (uint8_t)~AOTE_FLAG_TOUCHED;
        ML_$UNLOCK(AST_LOCK_ID);
        NETWORK_$AST_GET_INFO(&aote->obj_uid,
                              (uint16_t *)&ast_$purify_net_info_flags_00e01500,
                              attrs, &local_status);
        ML_$LOCK(AST_LOCK_ID);
        /* 0x00E0140C..0x00E01442 */
        if (local_status == status_$ok) {
            ML_$LOCK(PMAP_LOCK_ID);
            aote->dtu_high = attrs[0x24 / 4];               /* (-0x74,A6) */
            aote->dtu_low = (uint16_t)(attrs[0x28 / 4] >> 16); /* (-0x70,A6) */
            ML_$UNLOCK(PMAP_LOCK_ID);
        } else {
            aote->flags |= AOTE_FLAG_TOUCHED;
        }
        return;
    }

    /* 0x00E01446..0x00E0147E: TOUCHED -> stamp the DTU, mark DIRTY */
    if (aote->flags & AOTE_FLAG_TOUCHED) {
        aote->flags &= (uint8_t)~AOTE_FLAG_TOUCHED;
        ML_$LOCK(PMAP_LOCK_ID);
        TIME_$CLOCK((clock_t *)&aote->dtu_high);            /* pea (0x30,A2) */
        ML_$UNLOCK(PMAP_LOCK_ID);
        aote->flags |= AOTE_FLAG_DIRTY;
    }

    /* 0x00E01484..0x00E0148C */
    if ((aote->flags & AOTE_FLAG_DIRTY) == 0) {
        return;
    }

    /* 0x00E0148E..0x00E014A2: moveq #0x23 / dbf = 36 longwords from 0x0C */
    aote->flags &= (uint8_t)~AOTE_FLAG_DIRTY;
    src = (uint32_t *)&aote->obj_type;
    dst = attrs;
    for (i = 0x23; i >= 0; i--) {
        *dst++ = *src++;
    }

    /* 0x00E014A6..0x00E014DA: VTOCE_$WRITE(&obj_loc, &attrs, flags, status)
     * with the AST lock released */
    ML_$UNLOCK(AST_LOCK_ID);
    VTOCE_$WRITE((vtoc_$lookup_req_t *)(void *)&aote->obj_uid,
                 (vtoce_$result_t *)(void *)attrs, (char)flags, status);
    ML_$LOCK(AST_LOCK_ID);

    /* 0x00E014DC..0x00E014F0 */
    if (*status != status_$ok) {
        if (*status == status_$disk_write_protected) {      /* 0x80007 */
            *status = status_$ok;
        } else {
            *status |= (status_$t)0x80000000u;              /* bset.b #7 */
            aote->flags |= AOTE_FLAG_DIRTY;
        }
    }
}
