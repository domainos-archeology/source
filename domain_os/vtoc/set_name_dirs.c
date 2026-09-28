/*
 * VTOC_$SET_NAME_DIRS - Look up the two name directories of a volume
 *
 * Original address: 0x00E39486 (SAU2 map: VTOC_, VTOC_$SET_NAME_DIRS at
 * E39486)
 * Size: 102 bytes (0x00E39486 .. 0x00E394EB)
 *
 * Despite the name, the routine writes nothing into the per-volume record:
 * it builds a lookup request from each caller-supplied UID and the
 * corresponding stored VTOCE location hint (name_dir1 at -0x4C, name_dir2
 * at -0x48 from A5 + vol_idx*100) and runs VTOC_$LOOKUP on it, stopping
 * after the first one if that fails.  The second lookup's status is the
 * routine's result.
 *
 * Frame (link.w A6,-0x20; D2/D3/A2/A5 saved; A5 = 0xE784D0 = &vtoc_$data):
 *   (0x8,A6)   vol_idx     word -> D2w; D3w = vol_idx * 100
 *   (0xa,A6)   dir1_uid    pointer
 *   (0xe,A6)   dir2_uid    pointer
 *   (0x12,A6)  status_ret  pointer -> A2
 *   (-0x20,A6) req         the 0x20-byte lookup request; only its uid
 *                          (-0x18), block_hint (-0x1c) and vol_idx byte
 *                          (-0x4) are written
 *
 * Verified against the disassembly 2026-09-19; the body was already faithful.
 */

#include "vtoc/vtoc_internal.h"

void VTOC_$SET_NAME_DIRS(int16_t vol_idx, uid_t *dir1_uid, uid_t *dir2_uid,
                         status_$t *status_ret)
{
    vtoc_$vol_t *vol;               /* A5 + D3w - 0x54 */
    vtoc_$lookup_req_t req;         /* (-0x20,A6) */

    /* 0x00E394AC .. 0x00E394AE: `moveq #0x64,D3` / `mulu.w D2w,D3` */
    vol = VTOC_VOL(vol_idx);

    /* 0x00E3949C .. 0x00E394B0: req.uid = *dir1_uid, req.vol_idx = low byte
     * of vol_idx, req.block_hint = name_dir1 */
    req.uid = *dir1_uid;
    req.vol_idx = (uint8_t)vol_idx;
    req.block_hint = vol->name_dir1;

    /* 0x00E394B6 .. 0x00E394C0: VTOC_$LOOKUP(&req, status_ret) */
    VTOC_$LOOKUP(&req, status_ret);

    /* 0x00E394C2 tst.l (A2) / bne.b 0x00e394e2 */
    if (*status_ret != status_$ok) {
        return;
    }

    /* 0x00E394C6 .. 0x00E394D2: req.uid = *dir2_uid, req.block_hint = name_dir2
     * (the vol_idx byte is left from the first request) */
    req.uid = *dir2_uid;
    req.block_hint = vol->name_dir2;

    /* 0x00E394D8 .. 0x00E394DE: VTOC_$LOOKUP(&req, status_ret) */
    VTOC_$LOOKUP(&req, status_ret);
}
