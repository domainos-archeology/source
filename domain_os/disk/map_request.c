/*
 * disk/map_request.c - disk_$map_request
 *
 * Original address: 0x00E3CAE0, 408 bytes.  Was FUN_00e3cae0; renamed in
 * Ghidra as part of bead source-cm2w.
 *
 * Walks a chain of queue blocks and, for each one, turns the caller's disk
 * address into a physical volume plus (for a CHS device) a cylinder, head and
 * sector, then links the block onto that volume's list in the caller's map.
 *
 * A5 = 0x00E7A1CC = DISK_$DATA = DISK_VOLUME_BASE throughout, so a volume
 * descriptor is `A5 + idx*0x48` with its fields at +0x84 .. +0xb4, which is
 * what DISK_VOL(idx) expands to.
 *
 * Callers: DISK_$WRITE_MULTI (0x00E3CE02), DISK_$FORMAT_WHOLE (0x00E3D108)
 * and DISK_IO (0x00E3D61A).
 *
 * Every basic block of the original is accounted for; the addresses in the
 * comments say which instructions each statement stands for.
 */

#include "disk/disk_internal.h"

#include "math/math.h"

/*
 * Bit 9 of the device flag word at dev_info+8: the device has no cylinder /
 * head / sector geometry and takes the block number directly.  DISK_IO calls
 * the same bit DEV_FLAG_NO_HEADERS.
 */
#define DEV_FLAG_LINEAR_ADDRESS 0x0200 /* btst.l #0x9 at 0x00E3CB4E */

/*
 * The internal operation code DISK_IO assigns for a write
 * (DISK_INTERNAL_OP_WRITE in disk/io.c).  It is the only one the address
 * check treats specially.
 */
#define DISK_MAP_OP_WRITE 2 /* cmpi.w #0x2 at 0x00E3CB2A */

void disk_$map_request(disk_io_req_t *req, int16_t vol_idx, int16_t internal_op,
                       disk_$vol_map_entry_t *volume_map, status_$t *status)
{
    disk_$volume_t *vol; /* A1 */
    disk_io_req_t *next; /* D3 */
    uint32_t daddr;      /* D1 */
    uint16_t volx;       /* D2w: the physical volume this block lands on */
    uint16_t chunk_off;  /* D2w on the striped path */
    uint16_t dev_flags;  /* D0w */
    uint16_t remainder;  /* D0w / D6w */
    uint16_t sectors;    /* D1w / D6w */
    uint32_t sect32;     /* D5 */
    uint16_t cyl_quot;   /* D0w on the striped path */
    uint16_t member;     /* D1w on the striped path */

    *status = status_$ok; /* 0x00E3CAFC: clr.l (A0) */

    /* 0x00E3CAFE-0x00E3CB0A: A1 = &DISK_VOL(vol_idx) */
    vol = DISK_VOL(vol_idx);

    /* 0x00E3CB0E branches straight to the loop test at 0x00E3CC66 */
    while (req != NULL) {
        /* 0x00E3CB12: `move.l (A2),D3` -- the link is a 32-bit VA. */
        next = (disk_io_req_t *)ARCH_VA_TO_PTR(req->next);
        daddr = req->daddr;                           /* 0x00E3CB14 */

        /*
         * 0x00E3CB18-0x00E3CB3A.  An address below the volume's data size is
         * always fine.  At or above it the block is only reachable when the
         * volume is physical (lv_start == 0), the address is still inside the
         * device (below addr_end) and the operation is not a write.
         *
         *   0x00E3CB1C  bcs  -> below addr_start, accept
         *   0x00E3CB22  bcc  -> at or above addr_end, reject
         *   0x00E3CB28  bne  -> a logical volume, reject
         *   0x00E3CB2E  bne  -> not a write, accept
         */
        if (daddr >= vol->addr_start) {
            if (daddr >= vol->addr_end || vol->lv_start != 0 ||
                internal_op == DISK_MAP_OP_WRITE) {
                *status = status_$invalid_disk_address; /* 0x00E3CB34 */
                return;
            }
        }

        /* 0x00E3CB3E-0x00E3CB42: make the address absolute on the device */
        daddr += vol->lv_start;
        req->header[7] = daddr; /* move.l D1,(0x3c,A2) */

        /* 0x00E3CB46-0x00E3CB4A */
        dev_flags = *(uint16_t *)((uint8_t *)vol->dev_info + 8);

        /*
         * 0x00E3CB4E-0x00E3CB64: a device that takes a linear block number
         * cannot be striped.
         */
        if ((dev_flags & DEV_FLAG_LINEAR_ADDRESS) != 0 &&
            vol->part_volx[0] != 0) {
            *status = status_$disk_striping_not_supported; /* 0x00E3CB5E */
            return;
        }

        /*
         * 0x00E3CB68-0x00E3CB6E: the transfer length the driver may use, one
         * stripe chunk.  Written for striped and unstriped volumes alike.
         */
        req->flags = (uint16_t)(vol->stripe_blk_mask + 1);

        if (vol->part_volx[0] == 0) { /* 0x00E3CB72: not striped */
            volx = vol->part_volx[1]; /* 0x00E3CB78 */

            if ((dev_flags & DEV_FLAG_LINEAR_ADDRESS) != 0) {
                /* 0x00E3CBC2: the driver gets the block number as it is */
                req->daddr = daddr;
            } else {
                /*
                 * 0x00E3CB82-0x00E3CBC0.  `divu.w` puts the quotient in the
                 * low word, so only 16 bits of cylinder reach the request.
                 */
                req->daddr = (req->daddr & 0x0000FFFFu) |
                             ((uint32_t)(uint16_t)(daddr / vol->blocks_per_cyl)
                              << 16);

                /* 0x00E3CB8C-0x00E3CB9A: the block within the cylinder */
                remainder = (uint16_t)M$OIU$WLW((long)daddr,
                                                (short)vol->blocks_per_cyl);

                /* 0x00E3CB9E-0x00E3CBA6: blocks -> hardware sectors */
                sectors = (uint16_t)(remainder << vol->sector_size_code);
                sect32 = (uint32_t)sectors;

                /* 0x00E3CBAE-0x00E3CBBE */
                req->daddr = (req->daddr & 0xFFFF00FFu) |
                             (((sect32 / vol->sec_per_track) & 0xFFu) << 8);
                req->daddr = (req->daddr & 0xFFFFFF00u) |
                             ((sect32 % vol->sec_per_track) & 0xFFu);
            }
        } else {
            /*
             * 0x00E3CBC8-0x00E3CC30: the striped path.  See the stripe_*
             * fields in disk/disk_internal.h for the split.
             */
            chunk_off = (uint16_t)(vol->stripe_blk_mask & (uint16_t)daddr);
            daddr >>= vol->stripe_blk_shift; /* 0x00E3CBD4: lsr.l */

            /* 0x00E3CBD2-0x00E3CBE4 */
            remainder = (uint16_t)M$OIU$WLW((long)daddr,
                                            (short)vol->blocks_per_cyl);

            /* 0x00E3CBE6-0x00E3CBEE */
            sectors = (uint16_t)(remainder << vol->sector_size_code);
            sect32 = (uint32_t)sectors;

            /* 0x00E3CBF4-0x00E3CC04 */
            req->daddr = (req->daddr & 0xFFFF00FFu) |
                         (((sect32 / vol->sec_per_track) & 0xFFu) << 8);
            req->daddr = (req->daddr & 0xFFFFFF00u) |
                         ((sect32 % vol->sec_per_track) & 0xFFu);

            /* 0x00E3CC08-0x00E3CC0C: `divu.w`, so a 16-bit quotient */
            cyl_quot = (uint16_t)(daddr / vol->blocks_per_cyl);

            /* 0x00E3CC0E-0x00E3CC20 */
            member = (uint16_t)((cyl_quot & vol->stripe_vol_mask)
                                << vol->stripe_blk_shift);
            chunk_off = (uint16_t)(chunk_off + member);
            req->daddr = (req->daddr & 0x0000FFFFu) |
                         ((uint32_t)(uint16_t)(cyl_quot >>
                                               vol->stripe_vol_shift) << 16);
            chunk_off = (uint16_t)(chunk_off + 1);

            /*
             * 0x00E3CC26-0x00E3CC30: `ext.l` then `add.l`, so the index is
             * sign extended before it is doubled -- there is no bound check
             * against the nine entries of part_volx.
             */
            volx = *(uint16_t *)((uint8_t *)vol->part_volx +
                                 (int32_t)(int16_t)chunk_off * 2);
        }

        /*
         * 0x00E3CC34-0x00E3CC50: append the block to the chosen volume's
         * list.  The map is addressed as (-0x8,A3,volx*8) and
         * (-0x4,A3,volx*8) after `lsl.w #3` on the volume index, so entry
         * volx-1 of an array of {head, tail} pairs.  The index is not bounded
         * against DISK_VOLUME_MAP_ENTRIES, and volx = 0 would write below the
         * caller's array.
         */
        if (volume_map[volx - 1].head == NULL) {
            volume_map[volx - 1].head = req;
        } else {
            volume_map[volx - 1].tail->next = ARCH_PTR_TO_VA(req);
        }
        volume_map[volx - 1].tail = req;

        /* 0x00E3CC52-0x00E3CC60 */
        req->next = 0;
        req->op_flags = 0;
        req->op_flags &= 0xF0;
        req->op_flags |= (uint8_t)internal_op;

        req = next; /* 0x00E3CC64 */
    }
}
