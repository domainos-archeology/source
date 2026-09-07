/*
 * DISK_IO - the disk subsystem's read/write/format entry point
 *
 * Original address: 0x00e3d50e
 * Size: 1086 bytes
 *
 * Allocates a queue block, maps the request onto a physical volume with
 * disk_$map_request, hands it to the driver through DISK_$DO_IO, waits for
 * completion when the driver queues it, then verifies the block header and
 * (optionally) a software checksum before returning the header to the caller.
 *
 * A5 = 0xe7a1cc = DISK_$DATA = DISK_VOLUME_BASE throughout.
 *
 * Operation codes (jump table at 0xe3d574, entries for op 0..4):
 *   0 -> internal read   (DISK_$READ)
 *   1 -> internal write  (DISK_$WRITE, DISK_$AS_WRITE); write-protect checked
 *   2 -> internal read   (DISK_$AS_READ, DISK_$DIAG_IO read, the read-after-
 *        write verification below); skips the block-header check
 *   3 -> internal write  (DISK_$DIAG_IO write); write-protect checked
 *   4 -> internal format (DISK_$READ_MFG_BADSPOTS); skips the header check
 */

#include "disk/disk_internal.h"
#include "arch/arch.h"
#include "mmu/mmu.h"
#include "time/time.h"
#include "proc1/proc1.h"
#include "netlog/netlog.h"
#include "misc/crash_system.h"

/* Additional disk status codes */
#define status_$read_after_write_failed             0x0008001C
#define status_$software_detected_checksum_error    0x0008001F
#define status_$checksum_error_in_read_after_write  0x00080020

/* Recoverable driver errors that DISK_IO retries past (0xe3d77c-0xe3d792) */
#define status_$disk_recoverable_1  0x00080031
#define status_$disk_recoverable_2  0x0008002F
#define status_$disk_recoverable_3  0x00080030

/* Internal operation codes handed to disk_$map_request / DISK_$DO_IO */
#define DISK_INTERNAL_OP_READ   1
#define DISK_INTERNAL_OP_WRITE  2
#define DISK_INTERNAL_OP_FORMAT 9

/* Bits of dev_info->flags (the word at dev_info+8) */
#define DEV_FLAG_NO_HEADERS     0x0200  /* btst #9 (0xe3d8b2) */
#define DEV_FLAG_CHECKSUM       0x4000  /* btst #14 (0xe3d5d4) */

/* NETLOG record kinds */
#define NETLOG_KIND_DISK_READ   6
#define NETLOG_KIND_DISK_WRITE  7

/*
 * The disk module's second exclusion lock, DISK_$DATA + 0xa8, taken around
 * checksummed transfers (0xe3d678 / 0xe3d910).
 */
#define DISK_CHKSUM_LOCK    (&ml_$exclusion_t_00e7a274)

/*
 * The scratch physical page used for read-after-write verification lives at
 * DISK_$DATA + 0xaec (0xe3d820).
 */
#define DISK_RAW_PPN_CELL   (*(uint32_t *)(DISK_VOLUME_BASE + 0xaec))

/*
 * Sub-field accessors for the two overlapping regions of disk_io_req_t.
 * All of them are expressed with shifts and masks so that they mean the same
 * thing on a little-endian host as the byte and word accesses in the original
 * mean on m68k.
 */

/* word at req+0x16 = low half of the ppn longword at req+0x14 */
static inline uint16_t disk_req_count(const disk_io_req_t *req)
{
    return (uint16_t)(req->ppn & 0xFFFFu);
}

/* word at req+0x04 = high half of the daddr longword */
static inline uint16_t disk_req_daddr_hi(const disk_io_req_t *req)
{
    return (uint16_t)(req->daddr >> 16);
}

/* byte at req+0x06 */
static inline uint16_t disk_req_head(const disk_io_req_t *req)
{
    return (uint16_t)((req->daddr >> 8) & 0xFFu);
}

/* byte at req+0x07 */
static inline uint16_t disk_req_sector(const disk_io_req_t *req)
{
    return (uint16_t)(req->daddr & 0xFFu);
}

/* word at req+0x3a = low half of header[6] */
static inline uint16_t disk_req_chksum(const disk_io_req_t *req)
{
    return (uint16_t)(req->header[6] & 0xFFFFu);
}

static inline void disk_req_set_chksum(disk_io_req_t *req, uint16_t chksum)
{
    req->header[6] = (req->header[6] & 0xFFFF0000u) | chksum;
}

status_$t DISK_IO(uint16_t op, uint16_t vol_idx, uint32_t ppn, uint32_t daddr,
                  uint32_t *info)
{
    status_$t result;                   /* (-0x94,A6) */
    disk_$volume_t *vol;                /* A4: descriptor for the caller's volume */
    disk_$volume_t *io_vol;             /* A2: descriptor the transfer runs on */
    void *dev_info;                     /* (-0xb4,A6) */
    uint16_t dev_flags;
    /*
     * (-0x90,A6) and (-0x8c,A6) are the two four-byte VA cells
     * disk_$get_qblks_internal fills in (0x00E3BF7E, 0x00E3BFB8).  The
     * derived host pointers are what the rest of the body uses.
     */
    uint32_t req_va;                    /* (-0x90,A6) */
    uint32_t req_last_va;               /* (-0x8c,A6) */
    disk_io_req_t *req;                 /* A3, = ARCH_VA_TO_PTR(req_va) */
    void *req_last;
    int16_t internal_op;                /* D4w */
    boolean raw_op;                     /* (-0xaa,A6) */
    boolean do_header_check;            /* D6b */
    boolean do_checksum;                /* D3b */
    boolean lock_held;                  /* (-0xa2,A6) */
    char io_queued;                     /* (-0xa6,A6): DISK_$DO_IO result */
    int32_t io_ec_val;                  /* (-0x80,A6) */
    int32_t err_ec_val;                 /* (-0x7c,A6) */
    disk_$vol_map_entry_t volume_map[DISK_VOLUME_MAP_ENTRIES]; /* (-0x58,A6) */
    uint32_t verify_info[8];            /* (-0x78,A6) */
    status_$t verify_status;            /* (-0x84,A6): written, never read */
    uint16_t io_volx;                   /* D2w, initially the op */
    uint16_t log_kind;                  /* (-0x9a,A6) */
    uint8_t *per_proc;
    int16_t i;

    lock_held = false;                                  /* 0xe3d524 */

    /* 0xe3d528: bls -> unsigned compare */
    if (vol_idx > 10) {
        result = status_$invalid_volume_index;
        goto exit;
    }

    /* 0xe3d53a-0xe3d54a: clear the first longword of each of the ten
     * 8-byte physical-volume map entries (dbf #9 -> 10 iterations). */
    for (i = 0; i < DISK_VOLUME_MAP_ENTRIES; i++) {
        volume_map[i].head = NULL;
    }

    vol = DISK_VOL(vol_idx);                            /* 0xe3d54e */
    dev_info = vol->dev_info;                           /* 0xe3d55c */

    /*
     * 0xe3d562-0xe3d598: five-entry jump table.  NOTE: for op >= 5 the
     * original branches past the table with D4 (internal_op) never assigned,
     * so it carries whatever the caller left in the register.  There is no
     * way to reproduce that exactly in C; 0 is used and no caller passes an
     * op above 4.
     */
    internal_op = 0;
    switch (op) {
    case 0:
    case 2:
        internal_op = DISK_INTERNAL_OP_READ;            /* 0xe3d57e */
        break;
    case 1:
    case 3:
        internal_op = DISK_INTERNAL_OP_WRITE;           /* 0xe3d582 */
        /* 0xe3d584 btst.b #0,(0xa5,A4) */
        if ((vol->as_options & DISK_VOL_FLAG_WRITE_PROTECT) != 0) {
            result = status_$disk_write_protected;
            goto exit;
        }
        break;
    case 4:
        internal_op = DISK_INTERNAL_OP_FORMAT;          /* 0xe3d598 */
        break;
    default:
        break;
    }

    /*
     * 0xe3d59a-0xe3d5b2: a "raw" transfer skips the timestamp/checksum stamp.
     * raw = (op == 1 && volume flag bit 2) || op == 3
     */
    raw_op = (boolean)(((op == 1 &&
                         (vol->as_options & DISK_VOL_FLAG_NO_HDR_CHECK) != 0) ||
                        op == 3) ? -1 : 0);

    dev_flags = *(uint16_t *)((uint8_t *)dev_info + 8);

    /*
     * 0xe3d5b6-0xe3d5ce: header checking is on when the device's flag word is
     * non-negative and the operation is neither op 2 nor op 4.
     */
    do_header_check = (boolean)((((int16_t)dev_flags >= 0) &&
                                 op != 2 && op != 4) ? -1 : 0);

    /* 0xe3d5d0-0xe3d5da */
    do_checksum = (boolean)(((do_header_check < 0) &&
                             (dev_flags & DEV_FLAG_CHECKSUM) != 0) ? -1 : 0);

    /* 0xe3d5dc: mode 0xFF = write mode (this allocation must not block behind
     * queued readers) */
    disk_$get_qblks_internal(1, (int8_t)0xFF, &req_va, &req_last_va);
    req = ARCH_VA_TO_PTR(req_va);
    req_last = ARCH_VA_TO_PTR(req_last_va);

    /* 0xe3d5fa-0xe3d602: dbf #7 -> 8 longwords */
    for (i = 0; i < 8; i++) {
        req->header[i] = info[i];
    }

    req->daddr = daddr;                                 /* 0xe3d606 */

    disk_$map_request(req, (int16_t)vol_idx, internal_op,
                      volume_map, &req->status);        /* 0xe3d60c */
    if (req->status != status_$ok) {                    /* 0xe3d622 */
        goto cleanup;
    }

    if (op == 4) {
        /*
         * 0xe3d630-0xe3d63a: a format request carries the head number in the
         * low byte of daddr.  The head byte is req+0x06 (bits 8..15 of the
         * daddr longword), the sector byte req+0x07 (bits 0..7) is cleared
         * and the word at req+0x04 (bits 16..31) is cleared too.
         */
        req->daddr = (req->daddr & 0xFFFF0000u) | ((daddr & 0xFFu) << 8);
        req->daddr &= 0xFFFFFF00u;
        req->daddr &= 0x0000FFFFu;
    }

    /*
     * 0xe3d63e-0xe3d656: the transfer runs against the first physical volume
     * disk_$map_request marked in the map.  If it marked none, D2 keeps the
     * operation code it was loaded with at entry - reproduced here.
     */
    io_volx = op;
    for (i = 1; i <= DISK_VOLUME_MAP_ENTRIES; i++) {
        if (volume_map[i - 1].head != NULL) {
            io_volx = (uint16_t)i;
            break;
        }
    }

    req->ppn = ppn;                                     /* 0xe3d658 */

    /* 0xe3d65e-0xe3d670: bit 7 of req->op_flags mirrors volume flag bit 1 */
    req->op_flags &= 0x7F;
    if ((vol->as_options & 0x0002) != 0) {
        req->op_flags |= 0x80;
    }

    if (do_checksum < 0) {                              /* 0xe3d674 */
        ML_$EXCLUSION_START(DISK_CHKSUM_LOCK);
        lock_held = true;
    }

    /*
     * 0xe3d688-0xe3d6c4: stamp a write with the time and, when checksumming
     * is on, the page's checksum.  Raw writes and header-less devices are
     * left alone.
     */
    if (internal_op == DISK_INTERNAL_OP_WRITE &&
        (int16_t)dev_flags >= 0 && raw_op >= 0) {

        req->header[3] = TIME_$CLOCKH;                  /* long at req+0x2c */

        /* 0xe3d6a6/0xe3d6aa: clr.l (0x32,A3) and clr.l (0x36,A3) zero the
         * eight bytes req+0x32..req+0x39, i.e. the low half of header[4],
         * all of header[5] and the high half of header[6]. */
        req->header[4] &= 0xFFFF0000u;
        req->header[5] = 0;
        req->header[6] &= 0x0000FFFFu;

        if (do_checksum < 0) {
            disk_req_set_chksum(req, disk_$chksum_page(&req->ppn));
        } else {
            disk_req_set_chksum(req, 0);
        }
    }

    /* 0xe3d6c8: reads travel with the block number complemented so a stale
     * buffer cannot look valid */
    if (internal_op == DISK_INTERNAL_OP_READ) {
        req->header[2] = ~req->header[2];
    }

    MMU_$MCR_CHANGE(6);                                 /* 0xe3d6d2 */

    /* 0xe3d6e0-0xe3d708: snapshot this process's completion and error
     * eventcounts so disk_$wait_io knows what to wait for */
    per_proc = DISK_VOLUME_BASE + (int16_t)(PROC1_$CURRENT * DMOD_PER_PROC_SIZE);
    io_ec_val = *(int32_t *)(per_proc + DMOD_PER_PROC_IO_EC) + 1;
    err_ec_val = *(int32_t *)(per_proc + DMOD_PER_PROC_ERR_EC) + 1;

    io_vol = DISK_VOL(io_volx);                         /* 0xe3d70c */
    DISK_$DO_IO(io_vol, req, req, &io_queued);          /* 0xe3d71a */

    if (io_queued < 0) {                                /* 0xe3d730 */
        disk_$wait_io((int16_t)(1 << (io_volx & 0x1F)), &io_ec_val, &err_ec_val);
    }

    if (req->status != status_$ok) {                    /* 0xe3d74e */
        if (req->status == status_$disk_write_protected) {
            /* 0xe3d75c bset.b #0,(0xa5,A4) */
            vol->as_options |= DISK_VOL_FLAG_WRITE_PROTECT;
            goto cleanup;
        }

        disk_$io_error((int16_t)io_volx, req, info);    /* 0xe3d766 */

        /* 0xe3d778-0xe3d796 */
        if (req->status != status_$disk_recoverable_1 &&
            req->status != status_$disk_recoverable_2 &&
            req->status != status_$disk_recoverable_3) {
            goto cleanup;
        }
        req->status = status_$ok;
    }

    if (do_header_check < 0 && internal_op == DISK_INTERNAL_OP_READ) {
        /* 0xe3d7a4-0xe3d7c2: compare the UID and the block number the caller
         * asked for against what came back */
        if (info[0] != req->header[0] ||
            info[1] != req->header[1] ||
            info[2] != req->header[2]) {
            req->status = status_$disk_block_header_error;   /* 0xe3d7c4 */
            disk_$io_error((int16_t)io_volx, req, info);
            goto cleanup;
        }

        if (do_checksum < 0) {                          /* 0xe3d7e2 */
            uint16_t page_chksum = disk_$chksum_page(&req->ppn);

            /* 0xe3d7f4-0xe3d804: a zero stored checksum means "not stamped" */
            if (page_chksum != disk_req_chksum(req) &&
                disk_req_chksum(req) != 0) {
                verify_status = status_$ok;             /* 0xe3d808 */
                req->status = status_$software_detected_checksum_error;
                goto crash;
            }
        }
        goto log_it;
    }

    /*
     * 0xe3d816-0xe3d87e: read-after-write verification.  Only for
     * checksummed writes and only when the module has a scratch page.
     */
    if (internal_op == DISK_INTERNAL_OP_WRITE && do_checksum < 0 &&
        DISK_RAW_PPN_CELL != 0) {

        verify_status = DISK_IO(2, vol_idx, DISK_RAW_PPN_CELL, daddr,
                                verify_info);           /* 0xe3d826 */

        if (verify_status != status_$ok) {
            req->status = status_$read_after_write_failed;
            goto crash;
        }

        /* 0xe3d846-0xe3d856: dbne over 8 longwords - stops at the first
         * mismatch, so the trailing beq means "all eight matched" */
        for (i = 0; i < 8; i++) {
            if (verify_info[i] != req->header[i]) {
                req->status = status_$read_after_write_failed;
                goto crash;
            }
        }

        if (disk_$chksum_page(&DISK_RAW_PPN_CELL) != disk_req_chksum(req)) {
            req->status = status_$checksum_error_in_read_after_write;
            goto crash;
        }
    }

log_it:
    if (NETLOG_$OK_TO_LOG < 0) {                        /* 0xe3d88e */
        log_kind = (internal_op == DISK_INTERNAL_OP_READ)
                       ? NETLOG_KIND_DISK_READ : NETLOG_KIND_DISK_WRITE;

        if ((dev_flags & DEV_FLAG_NO_HEADERS) != 0) {
            /* 0xe3d8b8: a device without block headers logs only the count */
            NETLOG_$LOG_IT(log_kind, req->header, 0, 0,
                           disk_req_count(req), 0, 0, 0);
        } else {
            /* 0xe3d8c6-0xe3d8f6 */
            NETLOG_$LOG_IT(log_kind, req->header,
                           (uint16_t)(req->header[2] >> 5),
                           (uint16_t)(req->header[2] & 0x1F),
                           disk_req_count(req),
                           disk_req_daddr_hi(req),
                           disk_req_head(req),
                           disk_req_sector(req));
        }
    }
    goto cleanup;

crash:                                                  /* 0xe3d880 */
    CRASH_SYSTEM(&req->status);

cleanup:                                                /* 0xe3d90a */
    if (lock_held < 0) {
        ML_$EXCLUSION_STOP(DISK_CHKSUM_LOCK);
    }

    /* 0xe3d91c-0xe3d92e: hand the block header back for reads */
    if (internal_op == DISK_INTERNAL_OP_READ) {
        for (i = 0; i < 8; i++) {
            info[i] = req->header[i];
        }
    }

    result = req->status;                               /* 0xe3d932 */
    disk_$rtn_qblks_internal(1, req, req_last);         /* 0xe3d938 */

exit:                                                   /* 0xe3d948 */
    return result;
}
