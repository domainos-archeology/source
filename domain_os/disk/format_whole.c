/*
 * DISK_$FORMAT_WHOLE - Format every track of an assigned volume
 *
 * 0x00E3D296 - 0x00E3D394 (256 bytes, A5 = DISK_$DATA at 0xE7A1CC).
 * Verified against the disassembly on 2026-09-19; the earlier emission was
 * faithful apart from spelling the per-process eventcount slot as a
 * separate 0xE7A544 table (it is DISK_$DATA + 0x378 + 0x1C * pid, the
 * same cells disk/io.c reads).
 *
 * Arguments:
 *   (0x8,A6) vol_idx_ptr -> word volume index (D2)
 *   (0xc,A6) status      -> status_$t (A2)
 *
 * Frame: (-0x4,A6) chain tail VA, (-0x8,A6) err_ec value + 1,
 * (-0xc,A6) io_ec value + 1, (-0x10,A6) chain head VA, (-0x14,A6) the
 * byte DISK_$DO_IO's driver fills.
 */

#include "disk/disk_internal.h"
#include "arch/arch.h"

/* 0x00E3D33E: `ori.b #0xa,(0x1f,A4)` after `andi.b #-0x10` */
#define DISK_OP_FORMAT_WHOLE  0x0a

void DISK_$FORMAT_WHOLE(uint16_t *vol_idx_ptr, status_$t *status)
{
    uint16_t vol_idx;           /* D2 */
    disk_$volume_t *vol;        /* A3 (+0x7c form) */
    uint32_t head_va;           /* (-0x10,A6) */
    uint32_t tail_va;           /* (-0x4,A6) */
    disk_io_req_t *req;         /* A4 */
    int32_t io_ec_val;          /* (-0xc,A6) */
    int32_t err_ec_val;         /* (-0x8,A6) */
    uint8_t *per_proc;
    int8_t queued;              /* (-0x14,A6) */

    /* 0x00E3D2A4 - 0x00E3D2C2: `btst.l D0,D1` against 0x7fe, modulo 32 */
    vol_idx = *vol_idx_ptr;
    if ((((uint32_t)VALID_VOL_MASK >> (vol_idx & 0x1f)) & 1u) == 0) {
        *status = status_$invalid_volume_index;
        return;
    }

    /* 0x00E3D2C6 - 0x00E3D2EE: A5 + vol_idx * 0x48, fields at +0x90/+0x92 */
    vol = DISK_VOL(vol_idx);
    if (vol->mount_state != DISK_MOUNT_ASSIGNED ||
        (uint16_t)vol->mount_proc != PROC1_$CURRENT) {
        *status = status_$volume_not_properly_mounted;
        return;
    }

    /* 0x00E3D2F2 - 0x00E3D304: `move.l #0x10000,-(SP)` pushes count 1 and
     * mode 0 as two words */
    disk_$get_qblks_internal(1, 0, &head_va, &tail_va);
    req = (disk_io_req_t *)ARCH_VA_TO_PTR(head_va);

    /* 0x00E3D308 - 0x00E3D334: pid * 0x1c as a sign-extended word, the
     * eventcount values at A5+0x378 and A5+0x384 plus one */
    per_proc = DISK_VOLUME_BASE + (int16_t)(PROC1_$CURRENT * DMOD_PER_PROC_SIZE);
    io_ec_val = *(int32_t *)(per_proc + DMOD_PER_PROC_IO_EC) + 1;
    err_ec_val = *(int32_t *)(per_proc + DMOD_PER_PROC_ERR_EC) + 1;

    /* 0x00E3D338 - 0x00E3D33E */
    req->op_flags = (uint8_t)((req->op_flags & 0xf0) | DISK_OP_FORMAT_WHOLE);

    /* 0x00E3D344 - 0x00E3D356: the driver gets the descriptor (pea (0x7c,A3)),
     * the request twice and the byte cell */
    DISK_$DO_IO(vol, req, req, &queued);

    /* 0x00E3D35A - 0x00E3D374: queued -> wait for it.  The mask is a word,
     * `bset.l D2,D1` on a cleared word register; result slot discarded. */
    if (queued < 0) {
        disk_$wait_io((uint16_t)(1u << (vol_idx & 0x1f)), &io_ec_val, &err_ec_val);
    }

    /* 0x00E3D378 */
    *status = req->status;

    /* 0x00E3D37C - 0x00E3D388: result slot discarded */
    disk_$rtn_qblks_internal(1, req, ARCH_VA_TO_PTR(tail_va));
}
