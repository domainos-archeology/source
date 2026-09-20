/*
 * DISK_$FORMAT - Format one track of an assigned volume
 *
 * 0x00E3D396 - 0x00E3D50C (376 bytes, A5 = DISK_$DATA at 0xE7A1CC).
 * Re-emitted from the disassembly on 2026-09-19.  The earlier file
 * invented a disk_$rtn_qblks_internal call on the invalid-partition path:
 * the image jumps from 0x00E3D48A straight to the exit with the queue
 * block still allocated (a leak the original has).  It also read the
 * partition entry after the bounds test; the image reads it first
 * (0x00E3D472 before the `cmpi.w #0x8` at 0x00E3D476).
 *
 * Arguments:
 *   (0x8,A6)  vol_idx_ptr -> word volume index (D0)
 *   (0xc,A6)  cyl_ptr     -> word cylinder (D2)
 *   (0x10,A6) head_ptr    -> word absolute head number (D3)
 *   (0x14,A6) status      -> status_$t (A4)
 *
 * The head number is split by the volume's head count (+0x9e): the
 * quotient + 1 selects the partition (part_volx[1..8]), the remainder is
 * the head within it.  The request is then issued through the PARTITION
 * volume's driver.
 *
 * Frame: (-0x4,A6) err_ec + 1, (-0x8,A6) io_ec + 1, (-0xc,A6) chain tail,
 * (-0x10,A6) chain head, (-0x1e,A6) the driver's byte.
 */

#include "disk/disk_internal.h"
#include "arch/arch.h"

/* 0x00E3D3FE: `btst.l #0x9,D4` on the driver record's word +0x08 - the
 * same word DISK_$ADD_QUE (0x00E3C732) and DISK_$GET_MNT_INFO
 * (0x00E6BFDA) treat as the driver flags */
#define DISK_DEV_FLAG_NO_TRACK_FORMAT  0x0200
/* 0x00E3D4A8: `ori.b #0x3,(0x1f,A0)` after `andi.b #-0x10` */
#define DISK_OP_FORMAT_TRACK           0x03
/* 0x00E3D476: `cmpi.w #0x8,D4w` / bhi - part_volx has entries 1..8 */
#define DISK_FORMAT_MAX_PARTITION      8

void DISK_$FORMAT(uint16_t *vol_idx_ptr, uint16_t *cyl_ptr, uint16_t *head_ptr,
                  status_$t *status)
{
    uint16_t vol_idx;           /* D0 */
    uint16_t cylinder;          /* D2 */
    uint16_t head;              /* D3 */
    disk_$volume_t *vol;        /* A2 */
    disk_device_entry_t *dev;   /* A0 = (0x94,A2) */
    uint32_t head_va;           /* (-0x10,A6) */
    uint32_t tail_va;           /* (-0xc,A6) */
    disk_io_req_t *req;         /* A3 */
    int32_t io_ec_val;          /* (-0x8,A6) */
    int32_t err_ec_val;         /* (-0x4,A6) */
    uint8_t *per_proc;
    uint16_t part_idx;          /* D4: head / num_heads + 1 */
    uint16_t head_in_part;      /* D0 low word after swap: head % num_heads */
    uint16_t part_vol;          /* D3 */
    int8_t queued;              /* (-0x1e,A6) */

    /* 0x00E3D3A8 - 0x00E3D3C6 */
    vol_idx = *vol_idx_ptr;
    cylinder = *cyl_ptr;
    head = *head_ptr;
    if ((((uint32_t)VALID_VOL_MASK >> (vol_idx & 0x1f)) & 1u) == 0) {
        *status = status_$invalid_volume_index;                 /* 0x00E3D48A */
        return;
    }

    /* 0x00E3D3CA - 0x00E3D3F6: A5 + vol_idx * 0x48; +0x94 dev_info,
     * +0x90 mount_state, +0x92 mount_proc */
    vol = DISK_VOL(vol_idx);
    dev = (disk_device_entry_t *)vol->dev_info;
    if (vol->mount_state != DISK_MOUNT_ASSIGNED ||
        (uint16_t)vol->mount_proc != PROC1_$CURRENT) {
        *status = status_$volume_not_properly_mounted;
        return;
    }

    /* 0x00E3D3FA - 0x00E3D40A */
    if ((*(const uint16_t *)((const uint8_t *)dev + 0x08) &
         DISK_DEV_FLAG_NO_TRACK_FORMAT) != 0) {
        *status = status_$disk_illegal_request_for_device;
        return;
    }

    /* 0x00E3D40E - 0x00E3D420: count 1, mode 0 (one `move.l #0x10000`) */
    disk_$get_qblks_internal(1, 0, &head_va, &tail_va);
    req = (disk_io_req_t *)ARCH_VA_TO_PTR(head_va);

    /* 0x00E3D424 - 0x00E3D450: eventcount values + 1 for this process */
    per_proc = DISK_VOLUME_BASE + (int16_t)(PROC1_$CURRENT * DMOD_PER_PROC_SIZE);
    io_ec_val = *(int32_t *)(per_proc + DMOD_PER_PROC_IO_EC) + 1;
    err_ec_val = *(int32_t *)(per_proc + DMOD_PER_PROC_ERR_EC) + 1;

    /* 0x00E3D454 - 0x00E3D472: two `divu.w (0x9e,A2)` of the zero-extended
     * head; quotient + 1 is the partition index, the remainder (swap) the
     * head within it.  part_volx[part_idx] is fetched BEFORE the range
     * test. */
    part_idx = (uint16_t)((uint32_t)head / vol->num_heads) + 1;
    head_in_part = (uint16_t)((uint32_t)head % vol->num_heads);
    part_vol = vol->part_volx[part_idx];

    /* 0x00E3D476 - 0x00E3D490: no queue-block return on this path */
    if (part_idx > DISK_FORMAT_MAX_PARTITION ||
        (((uint32_t)VALID_VOL_MASK >> (part_vol & 0x1f)) & 1u) == 0) {
        *status = status_$invalid_volume_index;
        return;
    }

    /* 0x00E3D492 - 0x00E3D4A8: cylinder word at +0x04, head byte at +0x06,
     * sector byte +0x07 = 1, operation code 3 */
    req->daddr = ((uint32_t)cylinder << 16) |
                 ((uint32_t)(head_in_part & 0xff) << 8) | 0x01u;
    req->op_flags = (uint8_t)((req->op_flags & 0xf0) | DISK_OP_FORMAT_TRACK);

    /* 0x00E3D4AE - 0x00E3D4CE: through the partition volume's driver */
    DISK_$DO_IO(DISK_VOL(part_vol), req, req, &queued);

    /* 0x00E3D4D2 - 0x00E3D4EC */
    if (queued < 0) {
        disk_$wait_io((uint16_t)(1u << (part_vol & 0x1f)), &io_ec_val, &err_ec_val);
    }

    /* 0x00E3D4F0 */
    *status = req->status;

    /* 0x00E3D4F4 - 0x00E3D500: result slot discarded */
    disk_$rtn_qblks_internal(1, req, ARCH_VA_TO_PTR(tail_va));
}
