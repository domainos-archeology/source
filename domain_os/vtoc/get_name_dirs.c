/*
 * VTOC_$GET_NAME_DIRS - Get the two name-directory UIDs of a volume
 *
 * Original address: 0x00E393EE (SAU2 map: VTOC_, VTOC_$GET_NAME_DIRS at
 * E393EE)
 * Size: 152 bytes (0x00E393EE .. 0x00E39485)
 *
 * The per-volume VTOC record holds the VTOCE locations of the two name
 * directories (vtoc_$vol_t.name_dir2 at -0x48 and .name_dir1 at -0x4C from
 * A5 + vol_idx*100).  Each non-zero location is read with VTOCE_$READ and
 * the object UID at result+4 is returned.  A zero location for either one
 * is "no UID" (0x20004).  Only the first read's status is tested: the
 * second read's result is copied out whatever its status says.
 *
 * Frame (link.w A6,-0xb4; A2/A3/A5 saved; A5 = 0xE784D0 = &vtoc_$data):
 *   (0x8,A6)   vol_idx     word -> D0w, A2 = A5 + vol_idx*100
 *   (0xa,A6)   dir1_uid    pointer
 *   (0xe,A6)   dir2_uid    pointer
 *   (0x12,A6)  status_ret  pointer -> A3
 *   (-0x20,A6) req         the 0x20-byte lookup request; only its uid
 *                          (-0x18), block_hint (-0x1c) and vol_idx byte
 *                          (-0x4) are written
 *   (-0xb0,A6) result      the 0x90-byte VTOCE
 *
 * Verified against the disassembly 2026-09-19; the body was already faithful.
 */

#include "vtoc/vtoc_internal.h"

void VTOC_$GET_NAME_DIRS(int16_t vol_idx, uid_t *dir1_uid, uid_t *dir2_uid,
                         status_$t *status_ret)
{
    vtoc_$vol_t *vol;               /* A2 - 0x54 */
    vtoc_$lookup_req_t req;         /* (-0x20,A6) */
    vtoce_$result_t result;         /* (-0xb0,A6) */
    const uint32_t *uid_words;

    /* 0x00E393FC .. 0x00E39408: `moveq #0x64` / `mulu.w` / `lea (0x0,A5,D1w)` */
    vol = VTOC_VOL(vol_idx);

    /* 0x00E3940C tst.l (-0x48,A2) / beq.b 0x00e39454 */
    if (vol->name_dir2 != 0) {
        /* 0x00E39412 .. 0x00E39424: req.uid = UID_$NIL (0xE1737C),
         * req.vol_idx = low byte of vol_idx, req.block_hint = name_dir2 */
        req.uid = UID_$NIL;
        req.vol_idx = (uint8_t)vol_idx;
        req.block_hint = vol->name_dir2;

        /* 0x00E3942A .. 0x00E39438: VTOCE_$READ(&req, &result, status_ret) */
        VTOCE_$READ(&req, &result, status_ret);

        /* 0x00E3943C tst.l (A3) / bne.b 0x00e3947c */
        if (*status_ret != status_$ok) {
            return;
        }

        /* 0x00E39440 .. 0x00E3944A: *dir2_uid = result + 4 */
        uid_words = (const uint32_t *)(result.data + 0x04);
        dir2_uid->high = uid_words[0];
        dir2_uid->low = uid_words[1];

        /* 0x00E3944E tst.l (-0x4c,A2) / bne.b 0x00e3945c */
        if (vol->name_dir1 != 0) {
            /* 0x00E3945C move.l (-0x4c,A2),(-0x1c,A6) */
            req.block_hint = vol->name_dir1;

            /* 0x00E39462 .. 0x00E3946C: VTOCE_$READ(&req, &result, status_ret),
             * no status test afterwards */
            VTOCE_$READ(&req, &result, status_ret);

            /* 0x00E3946E .. 0x00E39478: *dir1_uid = result + 4 */
            uid_words = (const uint32_t *)(result.data + 0x04);
            dir1_uid->high = uid_words[0];
            dir1_uid->low = uid_words[1];
            return;
        }
    }

    /* 0x00E39454 move.l #0x20004,(A3) */
    *status_ret = status_$no_UID;
}
