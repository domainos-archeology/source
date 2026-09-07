/*
 * disk/io_error.c - disk_$io_error
 *
 * Original address: 0x00E3C14C, 544 bytes.  Was FUN_00e3c14c; renamed in
 * Ghidra as part of bead source-cm2w.
 *
 * Post-processes a failed disk request: it works the request's cylinder,
 * head and sector back into a linear disk address, records everything it
 * knows in the module's error block (the one DISK_$GET_ERROR_INFO hands out)
 * and adds a 16-byte type-12 entry to the system log.
 *
 * A5 = 0x00E7A1CC = DISK_$DATA = DISK_VOLUME_BASE throughout, so a volume
 * descriptor is `A5 + idx*0x48` with its fields at +0x84 .. +0xb4, which is
 * what DISK_VOL(idx) expands to.
 *
 * Callers: DISK_$READ_MULTI (0x00E3CF16), DISK_$FORMAT_WHOLE (0x00E3D22E) and
 * DISK_IO twice (0x00E3D770, 0x00E3D7D6).
 *
 * Every basic block of the original is accounted for; the addresses in the
 * comments say which instructions each statement stands for.
 */

#include "disk/disk_internal.h"

#include "log/log.h"
#include "math/math.h"
#include "misc/crash_system.h"
#include "time/time.h"

/*
 * Bit 9 of the device flag word at dev_info+8; see disk/map_request.c.  A
 * device with this bit set has no cylinder/head/sector geometry, so the
 * failing address is already linear.
 */
#define DEV_FLAG_LINEAR_ADDRESS 0x0200 /* btst.l #0x9 at 0x00E3C1AA */

/*
 * 0x00E3C36C: the status cell CRASH_SYSTEM is given when a linear-address
 * device turns out to be striped, reached by `pea (0x9c,PC)` at 0x00E3C2CE.
 */
static const status_$t disk_$striping_error_00e3c36c =
    status_$disk_striping_not_supported;

/*
 * The packed device identifier the log record carries, built twice by the
 * original at 0x00E3C210-0x00E3C244 and 0x00E3C246-0x00E3C27A (and once more
 * on the other path at 0x00E3C2DC-0x00E3C30C).
 *
 * The stores are Pascal packed-field assignments, each masking the target
 * before OR-ing the new value in, so they are written out here in the same
 * order rather than folded together:
 *
 *   clr.w   dst                  both bytes cleared
 *   andi.b  #0x07,dst            keep the low three bits
 *   or.b    (dev_info[5] << 3),dst
 *   andi.b  #0xF8,dst            drop the low three bits again
 *   or.b    dev_info[7],dst
 *   andi.b  #0x0F,dst+1          keep the low nibble
 *   or.b    (unit_lo << 4),dst+1
 */
static uint16_t disk_$pack_devid(const uint8_t *dev_info, uint8_t unit_lo)
{
    uint8_t hi = 0;
    uint8_t lo = 0;

    hi &= 0x07;
    hi |= (uint8_t)(dev_info[5] << 3);
    hi &= 0xF8;
    hi |= dev_info[7];

    lo &= 0x0F;
    lo |= (uint8_t)(unit_lo << 4);

    return (uint16_t)(((uint16_t)hi << 8) | lo);
}

void disk_$io_error(int16_t vol_idx, disk_io_req_t *req, uint32_t *info)
{
    disk_$volume_t *vol;    /* A3 / A0: the volume the caller asked for */
    disk_$volume_t *pv;     /* A1: the physical volume it maps onto */
    const uint8_t *dev_info;/* A4 */
    uint16_t pv_volx;       /* D0w */
    uint16_t part_off;      /* D2w */
    uint16_t dev_flags;     /* D4w */
    uint32_t blk_in_cyl;    /* D3 */
    uint32_t cyl_blocks;    /* D4 */
    uint32_t group;         /* D5 */
    uint32_t daddr;         /* D5, then the log record's first longword */
    uint32_t block;         /* D4 / D0: the log record's second longword */
    uint16_t vol_devid;     /* (-0x28,A6) */
    uint16_t pv_devid;      /* (-0x2a,A6) */
    disk_$error_log_t log;  /* (-0x18,A6) */
    int16_t i;

    /*
     * 0x00E3C15C-0x00E3C170: -1 means the request was never issued and
     * status_$invalid_disk_address was already reported by
     * disk_$map_request, so neither is worth logging.
     */
    if (req->status == (status_$t)-1 ||
        req->status == status_$invalid_disk_address) {
        return;
    }

    /* 0x00E3C174-0x00E3C182 */
    vol = DISK_VOL(vol_idx);

    /*
     * 0x00E3C184-0x00E3C192: part_volx[1] is the physical volume behind this
     * one.  When the caller's volume IS that volume the request did not go
     * through a partition, so the partition offset stays 0; otherwise it is
     * one less than the partition count.
     */
    pv_volx = vol->part_volx[1];
    part_off = 0;
    if ((uint16_t)vol_idx != pv_volx) {
        part_off = (uint16_t)(vol->num_parts - 1);
    }

    /* 0x00E3C194-0x00E3C1A6 */
    pv = DISK_VOL((int16_t)pv_volx);
    dev_info = (const uint8_t *)pv->dev_info;
    dev_flags = *(const uint16_t *)(dev_info + 8);

    if ((dev_flags & DEV_FLAG_LINEAR_ADDRESS) == 0) {
        /*
         * ---- the CHS path (0x00E3C1B2-0x00E3C2C6) --------------------
         *
         * Undo what disk_$map_request did: turn (cylinder, head, sector)
         * plus the partition offset back into a linear disk address.
         */

        /* 0x00E3C1B2-0x00E3C1C8: sectors within the cylinder, then blocks */
        blk_in_cyl = (uint32_t)((req->daddr >> 8) & 0xFFu) * pv->sec_per_track;
        blk_in_cyl += (uint32_t)(req->daddr & 0xFFu);
        blk_in_cyl >>= pv->sector_size_code;

        /* 0x00E3C1CA-0x00E3C1DC */
        cyl_blocks = (uint32_t)(uint16_t)(req->daddr >> 16) *
                     pv->blocks_per_cyl;

        /* 0x00E3C1D4-0x00E3C1DE: cylinder << stripe_vol_shift */
        group = (uint32_t)(uint16_t)(req->daddr >> 16)
                << pv->stripe_vol_shift;

        cyl_blocks += blk_in_cyl; /* 0x00E3C1DC: add.l D3,D4 */

        /* 0x00E3C1E0-0x00E3C1EC: + (partition offset >> stripe_blk_shift) */
        group += (uint32_t)part_off >> pv->stripe_blk_shift;

        /* 0x00E3C1E8-0x00E3C1FE */
        daddr = (uint32_t)M$MIU$LLW((ulong)group,
                                    (ushort)pv->blocks_per_cyl);
        daddr += blk_in_cyl;

        /* 0x00E3C200-0x00E3C20E */
        daddr <<= pv->stripe_blk_shift;
        daddr += (uint32_t)(uint16_t)(part_off & pv->stripe_blk_mask);

        /* 0x00E3C210-0x00E3C244: the caller's volume */
        vol_devid = disk_$pack_devid((const uint8_t *)vol->dev_info,
                                     (uint8_t)(vol->dev_unit & 0xFFu));

        /* 0x00E3C246-0x00E3C27A: the physical volume */
        pv_devid = disk_$pack_devid((const uint8_t *)pv->dev_info,
                                    (uint8_t)(pv->dev_unit & 0xFFu));

        /* 0x00E3C27C-0x00E3C288 */
        DISK_$ERROR_INFO.timestamp = TIME_$CURRENT_CLOCKH;
        DISK_$ERROR_INFO.daddr = daddr;
        DISK_$ERROR_INFO.geometry =
            ((uint32_t)pv->sec_per_track << 16) | pv->num_heads;

        /* 0x00E3C28E-0x00E3C29C: dbf #7 -> 8 longwords */
        for (i = 0; i < 8; i++) {
            DISK_$ERROR_INFO.info[i] = info[i];
        }

        /* 0x00E3C29E-0x00E3C2AC: dbf #7 -> 8 longwords */
        for (i = 0; i < 8; i++) {
            DISK_$ERROR_INFO.header[i] = req->header[i];
        }

        /* 0x00E3C2AE-0x00E3C2BC */
        DISK_$ERROR_INFO.vol_idx = (uint16_t)vol_idx;
        DISK_$ERROR_INFO.ppn = req->ppn;
        DISK_$ERROR_INFO.status = req->status;

        /* 0x00E3C2BE-0x00E3C2C2 */
        block = cyl_blocks;
    } else {
        /*
         * ---- the linear-address path (0x00E3C2C8-0x00E3C33C) ---------
         */

        /* 0x00E3C2C8-0x00E3C2D8 */
        if (pv->part_volx[0] != 0) {
            CRASH_SYSTEM(&disk_$striping_error_00e3c36c);
            return;
        }

        /*
         * 0x00E3C2DC-0x00E3C30C.  A4 still holds the PHYSICAL volume's
         * dev_info from 0x00E3C1A2, and the unit byte comes from the
         * physical volume too, so only one device id is built here.
         */
        pv_devid = disk_$pack_devid(dev_info,
                                    (uint8_t)(pv->dev_unit & 0xFFu));

        daddr = req->daddr; /* 0x00E3C30E: the whole longword */

        /* 0x00E3C312-0x00E3C32C */
        DISK_$ERROR_INFO.timestamp = TIME_$CURRENT_CLOCKH;
        DISK_$ERROR_INFO.daddr = daddr;
        DISK_$ERROR_INFO.vol_idx = (uint16_t)vol_idx;
        DISK_$ERROR_INFO.ppn = req->ppn;
        DISK_$ERROR_INFO.status = req->status;

        /*
         * 0x00E3C32E-0x00E3C33A: this path leaves the geometry, info and
         * header parts of the error block holding whatever the last CHS
         * failure left there, and both log device ids are the same word.
         */
        block = daddr;
        vol_devid = pv_devid;
    }

    /* 0x00E3C2BE / 0x00E3C336-0x00E3C34E: build the 16-byte log record */
    log.daddr = daddr;
    log.block = block;
    log.status = req->status;
    log.pv_devid = pv_devid;
    log.vol_devid = vol_devid;

    /* 0x00E3C350-0x00E3C35C */
    LOG_$ADD(DISK_LOG_TYPE_IO_ERROR, &log, (int16_t)sizeof(log));
}
