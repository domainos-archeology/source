/*
 * DISK Internal - Disk Subsystem Internal Definitions
 *
 * This header contains internal definitions used within the disk subsystem.
 * External code should use disk.h instead.
 */

#ifndef DISK_INTERNAL_H
#define DISK_INTERNAL_H

#include "disk/disk.h"
#include "uid/uid.h"
#include "ml/ml.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"
#include "dbuf/dbuf.h"
#include "time/time.h"
#include "misc/crash_system.h"
#include "arch/arch.h"   /* ARCH_VA_TO_PTR / ARCH_PTR_TO_VA */
#include "pmap/pmap.h"   /* PMAP_$DATA.mount_lock (MOUNT_LOCK) */

/*
 * The per-volume descriptor disk_$volume_t, its biased accessor DISK_VOL()
 * and the DISK_VOLUME_BASE / DISK_VOL_DESC_OFFSET constants now live in
 * disk/disk.h: the BAT manager reads the same DISK_$DVTBL entries at the
 * same 0x48 bias (BAT_$MOUNT 0x00E3B820-0x00E3B858), so the record is part
 * of the disk subsystem's public interface (bead source-9ddf).
 */

/*
 * Bits of the low byte of disk_$volume_t.as_options (+0xa5, -0x1f).
 * These are addressed with byte bit instructions in the original; on
 * big-endian m68k the low byte's bit n is bit n of the containing word.
 */
#define DISK_VOL_FLAG_WRITE_PROTECT  0x0001  /* 0xe3d98c bset.b #0,(0xa5,A1) */
#define DISK_VOL_FLAG_NO_HDR_CHECK   0x0004  /* 0xe3d5a0 btst.b #2,(0xa5,A4) */

/*
 * disk_$dev_ops_t - the driver entry vector a volume's dev_info points at
 *
 * disk_$volume_t.dev_info (+0x18) holds the address of a device descriptor
 * whose first longword is the address of this vector.  Only the entry the
 * disk module itself reaches by offset is named:
 *
 *   00e3db80  movea.l (0x18,A2),A1      ; A1 = vol->dev_info
 *   00e3db84  movea.l (A1),A0           ; A0 = *dev_info = the vector
 *   00e3db86  move.l  (0xc,A0),D2       ; D2 = the revalidate entry
 *
 * The entries below 0x0C are reached through other paths and are left
 * unnamed rather than guessed.
 */
typedef struct disk_$dev_ops_t {
    uint32_t    _entry_00;          /* 0x00 */
    uint32_t    _entry_04;          /* 0x04 */
    uint32_t    _entry_08;          /* 0x08 */
    uint32_t    revalidate;         /* 0x0C: called by DISK_$REVALID */
} disk_$dev_ops_t;

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(disk_$dev_ops_t, revalidate) == 0x0C,
               "disk_$dev_ops_t.revalidate");
#endif

/*
 * DISK_$REVALID - dispatch a media-change revalidate to the driver
 *
 * Takes the volume descriptor itself, not an index: DISK_$REVALIDATE hands
 * it DISK_VOL(vol_idx) (0x00E6C07A `pea (-0x48,A0,D0w*0x1)` with
 * A0 = 0x00E7A290 and D0 = vol_idx * 0x48).
 *
 * Original address: 0x00E3DB74
 */
void DISK_$REVALID(struct disk_$volume_t *vol);


/* Mount states */
#define DISK_MOUNT_FREE      0
#define DISK_MOUNT_RESERVED  1
#define DISK_MOUNT_ASSIGNED  2
#define DISK_MOUNT_BUSY      3
#define DISK_MOUNT_MIRROR    4

/* Valid volume index mask (volumes 1-10) - bits 1-10 set */
#define VALID_VOL_MASK  0x7fe

/* Maximum logical volume index */
#define MAX_LV_INDEX  10

/* Number of volume table entries to scan (indices 1-6) */
#define VOL_TABLE_SCAN_COUNT  6

/* PV_LABEL_$UID (0xe1738c) and LV_LABEL_$UID (0xe17394) come from uid/uid.h */

/*
 * Disk module data layout (A5-relative offsets)
 *
 * The disk subsystem uses a Pascal module data area at DISK_$DATA (0xe7a1cc).
 * Internal functions access fields through byte offsets from this base.
 * In the original m68k code, A5 holds the base pointer.
 */
#define DMOD_EVENTCOUNT       0x000  /* ec_$eventcount_t - module eventcount */
#define DMOD_REQ_QUEUE        0x00E  /* int16_t[64] - circular request buffer (1-indexed) */
#define DMOD_EXCLUSION        0x090  /* ml_$exclusion_t - module exclusion lock */
#define DMOD_RESERVE_BLOCK    0x0BC  /* uint32_t VA - reserve block for write-mode allocation */
#define DMOD_FREE_HEAD        0x0C0  /* uint32_t VA - free list head */
#define DMOD_PAGES_ALLOC      0xAF0  /* int16_t - pool pages allocated */
#define DMOD_REQ_READ_IDX     0xAF2  /* int16_t - request queue read index */
#define DMOD_REQ_WRITE_IDX    0xAF4  /* int16_t - request queue write index */
#define DMOD_PENDING_COUNT    0xAF6  /* int16_t - pending request count */
#define DMOD_AVAIL_COUNT      0xAF8  /* int16_t - available block count */
#define DMOD_ALLOC_DISABLED   0xAFA  /* int8_t - pool growth disabled (0xFF=disabled) */
#define DMOD_RESERVE_AVAIL    0xAFC  /* int8_t - reserve block available (0xFF=available) */

/* Request queue size (entries 1..64, circular) */
#define DMOD_REQ_QUEUE_SIZE   0x40

/*
 * Per-process disk data (A5-relative)
 *
 * Each process has a 0x1c-byte slot in the disk module data area.
 * The slot base is at: data + PROC1_$CURRENT * DMOD_PER_PROC_SIZE.
 * Within each slot, eventcounts for I/O completion and error
 * notification are at fixed offsets from the slot base.
 */
#define DMOD_PER_PROC_SIZE    0x1c  /* Per-process slot size (28 bytes) */
#define DMOD_PER_PROC_IO_EC   0x378 /* ec_$eventcount_t: I/O completion EC (offset from slot base) */
#define DMOD_PER_PROC_ERR_EC  0x384 /* ec_$eventcount_t: error EC (offset from slot base) */

/*
 * Volume descriptor field offsets (relative to volume entry start)
 *
 * Volume N starts at: data + N * DISK_VOLUME_SIZE (0x48).
 * Disk indices 1-10 are used; index 0 is reserved.
 */
#define DMOD_VOL_ERROR_QUE    0x7c  /* Error queue pointer (passed to DISK_$ERROR_QUE) */

/* Number of disk volumes to check in wait/error loops */
#define DMOD_NUM_VOLUMES      10

/* Timeout for disk I/O wait (in TIME_$CLOCKH ticks, ~240 ticks) */
#define DMOD_WAIT_TIMEOUT     0xf0

/*
 * Queue block field offsets
 *
 * Queue blocks (disk I/O request blocks) are linked in free and allocated
 * chains. These offsets are used for initialization during allocation.
 */
#define DISK_QBLK_FORWARD     0x00  /* uint32_t VA - next in allocated chain */
#define DISK_QBLK_FREE_NEXT   0x08  /* uint32_t VA - next in free list */
#define DISK_QBLK_STATUS      0x0C  /* uint32_t - I/O status */
#define DISK_QBLK_PHYS        0x10  /* uint32_t - physical address of the
                                     * block's own +0x20 (disk_$grow_qblk_pool
                                     * 0x00E3BDDA-0x00E3BDE8) */
#define DISK_QBLK_FLAGS       0x1C  /* uint16_t - I/O flags */
#define DISK_QBLK_OWNER       0x1E  /* uint8_t - owning process ID */
#define DISK_QBLK_RESERVED    0x1F  /* uint8_t - reserved */

/*
 * Internal data structures
 */

/*
 * Diagnostic mode flag - enables special diagnostic I/O operations.
 * DISK_$DIAG is an accessor into DISK_$DATA; see disk/disk.h.
 */

/*
 * Mount lock - protects mount table operations.  The SAU2 map puts
 * MOUNT_LOCK (0xE254B8) inside the PMAP_ data segment, so it is the field
 * PMAP_$DATA.mount_lock of that module block (pmap/pmap.h, source-iq58).
 */

/*
 * Exclusion locks for disk operations.  Both live inside the DISK_ module
 * block, so they are accessors into DISK_$DATA rather than storage of their
 * own: DMOD_EXCLUSION (0x90) is 0x00E7A25C and 0xa8 is 0x00E7A274.
 */
_Static_assert(DMOD_EXCLUSION + sizeof(ml_$exclusion_t) <= DISK_$DATA_SIZE,
               "disk module exclusion lock must lie inside DISK_$DATA");
#define ml_$exclusion_t_00e7a25c (*(ml_$exclusion_t *)&DISK_$DATA[DMOD_EXCLUSION])

extern ml_$exclusion_t ml_$exclusion_t_00e7a274;  /* DISK_$DATA +0xa8 */

/*
 * Error status variables used for CRASH_SYSTEM calls
 * (declared as status_$t in misc/crash_system.h, which this header includes).
 */

/*
 * Disk I/O request (queue block), 0x40 bytes
 *
 * Allocated by disk_$get_qblks_internal and handed to DISK_$DO_IO.  Field
 * offsets verified against DISK_IO (0xe3d50e), disk_$get_qblks_internal
 * (0xe3be8a) and disk_$map_request (0xe3cae0).
 *
 * Two regions are addressed at sub-field granularity by the original:
 *   - the longword at +0x04 is the disk address, but a format request
 *     overwrites its bytes at +0x06 (head) and +0x07 (sector) and clears the
 *     word at +0x04 (0xe3d630-0xe3d63a); disk_$map_request writes the same
 *     bytes for a CHS device.
 *   - the block header at +0x20 is copied to and from the caller's 8-longword
 *     info array, but DISK_IO also plants a timestamp at +0x2c (header[3]),
 *     clears +0x32..+0x39 and stores a 16-bit checksum at +0x3a.
 * The disk_req_* helpers in disk/io.c express those accesses with shifts and
 * masks so they are byte-order independent.
 */
typedef struct disk_io_req_t {
    uint32_t    next;               /* 0x00: VA of the next block in the
                                     *   allocated chain.  A 32-bit target
                                     *   address, not a host pointer: the
                                     *   original moves it with `move.l`
                                     *   (disk_$get_qblks_internal 0x00E3BE8A
                                     *   builds the chain with
                                     *   `move.l (0x8,A2),(A2)`, DISK_IO
                                     *   0x00E3D50E hands the cell around as
                                     *   four bytes) and the live daddr field
                                     *   sits immediately above it.  Convert
                                     *   with ARCH_VA_TO_PTR / ARCH_PTR_TO_VA,
                                     *   which are identity casts on m68k. */
    uint32_t    daddr;              /* 0x04: disk address / (head, sector) */
    uint32_t    free_next;          /* 0x08: VA of the next block in the free
                                     *   list.  Same four-byte cell:
                                     *   disk_$rtn_qblks_internal writes it
                                     *   with `move.l (0xc0,A5),(0x8,A2)`
                                     *   (0x00E3C056) and status at +0x0c sits
                                     *   immediately above it. */
    status_$t   status;             /* 0x0c: result status */
    uint32_t    reserved_10;        /* 0x10 */
    uint32_t    ppn;                /* 0x14: physical page number; its low word
                                     *   (+0x16) is what NETLOG logs */
    uint32_t    reserved_18;        /* 0x18 */
    uint16_t    flags;              /* 0x1c: the transfer length the driver may
                                     *   use, one stripe chunk.
                                     *   disk_$map_request stores
                                     *   stripe_blk_mask + 1 here for striped
                                     *   and unstriped volumes alike
                                     *   (0xe3cb68-0xe3cb6e). */
    uint8_t     owner;              /* 0x1e: owning process id */
    uint8_t     op_flags;           /* 0x1f: low nibble = operation code,
                                     *   bit 7 = "checksum this transfer" */
    uint32_t    header[8];          /* 0x20: on-disk block header.  DISK_IO
                                     *   copies the caller's eight longwords
                                     *   in here (0xe3d5fa) and
                                     *   disk_$map_request then overwrites
                                     *   header[7] (+0x3c) with the absolute
                                     *   disk address the block will live at
                                     *   (0xe3cb42), so the header is self
                                     *   identifying. */
} disk_io_req_t;

/*
 * Every field is a fixed-width cell, so the whole 0x40-byte layout is checked
 * on the host build too (bead source-wyn9; documented offsets, bead
 * source-pewa).
 */
_Static_assert(__builtin_offsetof(disk_io_req_t, next) == 0x00, "disk_io_req_t.next");
_Static_assert(__builtin_offsetof(disk_io_req_t, daddr) == 0x04, "disk_io_req_t.daddr");
_Static_assert(__builtin_offsetof(disk_io_req_t, free_next) == 0x08, "disk_io_req_t.free_next");
_Static_assert(__builtin_offsetof(disk_io_req_t, reserved_10) == 0x10, "disk_io_req_t.reserved_10");
_Static_assert(__builtin_offsetof(disk_io_req_t, reserved_18) == 0x18, "disk_io_req_t.reserved_18");
_Static_assert(__builtin_offsetof(disk_io_req_t, owner) == 0x1E, "disk_io_req_t.owner");

_Static_assert(sizeof(disk_io_req_t) == 0x40,
               "disk_io_req_t must be 0x40 bytes");
_Static_assert(__builtin_offsetof(disk_io_req_t, status) == 0x0c,
               "disk_io_req_t.status must be at 0x0c");
_Static_assert(__builtin_offsetof(disk_io_req_t, ppn) == 0x14,
               "disk_io_req_t.ppn must be at 0x14");
_Static_assert(__builtin_offsetof(disk_io_req_t, op_flags) == 0x1f,
               "disk_io_req_t.op_flags must be at 0x1f");
_Static_assert(__builtin_offsetof(disk_io_req_t, header) == 0x20,
               "disk_io_req_t.header must be at 0x20");

/*
 * DISK_IO - the disk subsystem's read/write/format entry point
 *
 * Original address: 0x00e3d50e
 *
 * @param op       0 = read, 1 = write, 2 = read without header check,
 *                 3 = raw write, 4 = format (jump table at 0xe3d574)
 * @param vol_idx  Volume index (0-10)
 * @param ppn      Physical page number of the transfer buffer (arg 3 at
 *                 (0xc,A6), stored to req+0x14 at 0xe3d658)
 * @param daddr    Disk address (arg 4 at (0x10,A6), stored to req+0x04 at
 *                 0xe3d606)
 * @param info     8-longword block header, copied in before the transfer and
 *                 back out afterwards for reads
 */
status_$t DISK_IO(uint16_t op, uint16_t vol_idx, uint32_t ppn, uint32_t daddr,
                  uint32_t *info);

/* Status codes the three internal helpers below load directly */
#define status_$invalid_disk_address          0x00080012
#define status_$disk_striping_not_supported   0x0008002d

/*
 * The per-request physical-volume map disk_$map_request fills in and DISK_IO
 * scans.  Ten entries of two longwords each; entry v-1 belongs to volume v,
 * because the original addresses it as (-0x8,A3,volx*8) / (-0x4,A3,volx*8)
 * (0xe3cc38-0xe3cc4e).
 */
#define DISK_VOLUME_MAP_ENTRIES 10

/*
 * One entry of that map: the head and tail of the chain of queue blocks that
 * landed on this physical volume.  Eight bytes on the target, which is the
 * stride the original's (volx*8) indexing assumes.
 */
typedef struct disk_$vol_map_entry_t {
    struct disk_io_req_t *head; /* (-0x8,A3,volx*8) */
    struct disk_io_req_t *tail; /* (-0x4,A3,volx*8) */
} disk_$vol_map_entry_t;

/*
 * These two stay host pointers: the map is a caller stack array that never
 * reaches the hardware or another record, so only the target build has to
 * match the 8-byte stride the original's (volx*8) indexing assumes.  The
 * request chain the map points into is a VA chain -- see disk_io_req_t.next.
 */

#if defined(ARCH_M68K)
_Static_assert(sizeof(disk_$vol_map_entry_t) == 8,
               "disk_$vol_map_entry_t must be 8 bytes");
#endif

/*
 * The module's disk error record, DISK_$DATA + 0xa94 = 0xe7ac60.  86 bytes,
 * which is exactly what DISK_$GET_ERROR_INFO copies out (21 longwords plus a
 * word).  Written only by disk_$io_error.
 */
typedef struct disk_$error_info_t {
    uint32_t    timestamp;      /* +0x00 (A5+0xa94): TIME_$CURRENT_CLOCKH */
    uint32_t    daddr;          /* +0x04 (A5+0xa98): the failing disk address */
    uint32_t    geometry;       /* +0x08 (A5+0xa9c): the longword at the
                                 *   physical volume's +0x9c, i.e.
                                 *   sec_per_track in the high half and
                                 *   num_heads in the low half.  Only the
                                 *   CHS path writes it (0xe3c288). */
    uint32_t    info[8];        /* +0x0c (A5+0xaa0): the caller's block header,
                                 *   CHS path only (0xe3c298) */
    uint32_t    header[8];      /* +0x2c (A5+0xac0): the request's block
                                 *   header, CHS path only (0xe3c2a8) */
    uint32_t    ppn;            /* +0x4c (A5+0xae0) */
    status_$t   status;         /* +0x50 (A5+0xae4) */
    uint16_t    vol_idx;        /* +0x54 (A5+0xae8) */
} disk_$error_info_t;

#if defined(ARCH_M68K)
_Static_assert(sizeof(disk_$error_info_t) == 0x56,
               "disk_$error_info_t must be 86 bytes");
_Static_assert(__builtin_offsetof(disk_$error_info_t, info) == 0x0c,
               "disk_$error_info_t.info must be at +0x0c (A5+0xaa0)");
_Static_assert(__builtin_offsetof(disk_$error_info_t, header) == 0x2c,
               "disk_$error_info_t.header must be at +0x2c (A5+0xac0)");
_Static_assert(__builtin_offsetof(disk_$error_info_t, vol_idx) == 0x54,
               "disk_$error_info_t.vol_idx must be at +0x54 (A5+0xae8)");
#endif

#define DISK_$ERROR_INFO \
    (*(disk_$error_info_t *)(DISK_VOLUME_BASE + 0xa94))

/*
 * The 16-byte record disk_$io_error hands to LOG_$ADD as log type 12
 * (0xe3c350-0xe3c35c).  It is built on the stack at A6-0x18.
 */
typedef struct disk_$error_log_t {
    uint32_t    daddr;          /* -0x18: the failing disk address */
    uint32_t    block;          /* -0x14: cyl*blocks_per_cyl + block-in-cyl on
                                 *   the CHS path, the raw address otherwise */
    status_$t   status;         /* -0x10 */
    uint16_t    pv_devid;       /* -0x0c: packed device id of the physical
                                 *   volume the transfer ran on */
    uint16_t    vol_devid;      /* -0x0a: packed device id of the volume the
                                 *   caller asked for (the same value on the
                                 *   non-CHS path, 0xe3c330) */
} disk_$error_log_t;

#if defined(ARCH_M68K)
_Static_assert(sizeof(disk_$error_log_t) == 0x10,
               "disk_$error_log_t must be 16 bytes");
#endif

#define DISK_LOG_TYPE_IO_ERROR 12

/*
 * disk_$map_request - resolve a request's disk address to a physical volume
 *
 * Walks the request chain, adds the logical volume's start block, checks the
 * address against the volume's range and fills in the cylinder/head/sector
 * fields.  Entry i of the 10-entry map (8 bytes each) is left non-zero for
 * each physical volume the request touches; DISK_IO uses the first such entry
 * as the volume to issue the transfer against (0xe3d63e).
 *
 * Original address: 0x00e3cae0 (was FUN_00e3cae0); see disk/map_request.c
 */
void disk_$map_request(disk_io_req_t *req, int16_t vol_idx, int16_t internal_op,
                       disk_$vol_map_entry_t *volume_map, status_$t *status);

/*
 * disk_$io_error - post-process a failed disk request
 *
 * Original address: 0x00e3c14c (was FUN_00e3c14c); see disk/io_error.c
 */
void disk_$io_error(int16_t vol_idx, disk_io_req_t *req, uint32_t *info);

/*
 * disk_$chksum_page - checksum one physical page
 *
 * Temporarily maps the page at the scratch virtual address 0xff8400, runs
 * CHKSUM_$GET_CHKSUM over it and restores the previous mapping.
 *
 * Original address: 0x00e0a290 (was FUN_00e0a290); see disk/chksum_page.c
 */
uint16_t disk_$chksum_page(uint32_t *ppn);


/*
 * disk_$pv_label_t - the physical volume label, disk block 0 of a volume
 * (DISK_$GET_BLOCK with PV_LABEL_$UID, disk_$validate_pv_label 0x00E6C22E).
 * Fields up to +0x9A follow the classic label (apollofs PVLabel: version,
 * "APOLLO", name, uid, drive type, total blocks, blocks per track, tracks
 * per cylinder, LV and alternate LV label daddrs, bad-spot and diagnostic
 * cylinders, sector start / size, precompensation cylinder); the words from
 * +0xA0 are the SR10 striping extension DISK_$PV_MOUNT_INTERNAL reads.
 * Only cells the kernel reads are named.  Pointer-free.
 */
typedef struct disk_$pv_label_t {
    int16_t     version;            /* 0x00: > 1 (unsigned) is refused
                                     *   (0x00E6C266) */
    char        apollo[6];          /* 0x02: "APOLLO" (0x00E6C270) */
    char        name[32];           /* 0x08 */
    uid_t       pv_uid;             /* 0x28: copied to the descriptor's
                                     *   lv_uid (0x00E6C45A) and compared
                                     *   across a striped set (0x00E6C636) */
    uint16_t    reserved_30;        /* 0x30 */
    uint16_t    drive_type;         /* 0x32: becomes unit_id for unit type 4
                                     *   when its high byte is set
                                     *   (0x00E6C42E) */
    uint32_t    total_blocks;       /* 0x34: -> addr_start (0x00E6C43E) */
    uint16_t    blocks_per_track;   /* 0x38: -> sec_per_track (0x00E6C454) */
    uint16_t    tracks_per_cyl;     /* 0x3A: -> num_heads */
    uint32_t    lv_daddr[10];       /* 0x3C */
    uint32_t    alt_lv_daddr[10];   /* 0x64 */
    uint32_t    badspot_daddr;      /* 0x8C */
    uint32_t    diag_daddr;         /* 0x90 */
    uint16_t    sector_start;       /* 0x94 */
    uint16_t    sector_size;        /* 0x96 */
    uint16_t    precomp_cyl;        /* 0x98: handed back in pvlabel_info+4
                                     *   (0x00E6C476) */
    uint16_t    reserved_9a[3];     /* 0x9A */
    uint32_t    addr_end;           /* 0xA0: -> addr_end, 0 meaning
                                     *   0xFFFFFF (0x00E6C444) */
    uint32_t    reserved_a4;        /* 0xA4 */
    uint16_t    num_parts;          /* 0xA8: volumes in a striped set; 0, 1,
                                     *   2, 4 or 8 (0x00E6C284) */
    uint16_t    part_num;           /* 0xAA: this volume's 1-based place in
                                     *   the set (0x00E6C4F0, 0x00E6C648) */
    uint16_t    part_dev[8];        /* 0xAC: entry k+1 names member k+1:
                                     *   high byte & 7 = device, low byte
                                     *   bits 4..7 = unit (0x00E6C55C) */
    uint16_t    interleave;         /* 0xBC: the striping mode, 0..5; only
                                     *   0, 1, 2, 4, 5 (mask 0x37) are legal
                                     *   with more than one member
                                     *   (0x00E6C258) */
    uint16_t    sectors_per_block;  /* 0xBE: hardware sectors per disk
                                     *   block (1, 2, 4, 8); its log2 via
                                     *   DISK_$MOUNT_DATA.log2_table is the
                                     *   sector_size_code (0x00E6C466) */
} disk_$pv_label_t;

_Static_assert(__builtin_offsetof(disk_$pv_label_t, apollo) == 0x02, "pv_label.apollo");
_Static_assert(__builtin_offsetof(disk_$pv_label_t, pv_uid) == 0x28, "pv_label.pv_uid");
_Static_assert(__builtin_offsetof(disk_$pv_label_t, drive_type) == 0x32, "pv_label.drive_type");
_Static_assert(__builtin_offsetof(disk_$pv_label_t, total_blocks) == 0x34, "pv_label.total_blocks");
_Static_assert(__builtin_offsetof(disk_$pv_label_t, blocks_per_track) == 0x38, "pv_label.blocks_per_track");
_Static_assert(__builtin_offsetof(disk_$pv_label_t, precomp_cyl) == 0x98, "pv_label.precomp_cyl");
_Static_assert(__builtin_offsetof(disk_$pv_label_t, addr_end) == 0xA0, "pv_label.addr_end");
_Static_assert(__builtin_offsetof(disk_$pv_label_t, num_parts) == 0xA8, "pv_label.num_parts");
_Static_assert(__builtin_offsetof(disk_$pv_label_t, part_num) == 0xAA, "pv_label.part_num");
_Static_assert(__builtin_offsetof(disk_$pv_label_t, part_dev) == 0xAC, "pv_label.part_dev");
_Static_assert(__builtin_offsetof(disk_$pv_label_t, interleave) == 0xBC, "pv_label.interleave");
_Static_assert(__builtin_offsetof(disk_$pv_label_t, sectors_per_block) == 0xBE, "pv_label.sectors_per_block");

/*
 * DISK_$MOUNT_DATA - the third DISK_ module data block, `D E826C4 DISK_
 * size = 64` in the SAU2 map (no interior symbols).  The volume-mount
 * routines (DISK_$PV_MOUNT, DISK_$PV_ASSIGN_N, DISK_$LV_ASSIGN,
 * DISK_$DISMOUNT, ...) all load it as A5; DISK_$PV_MOUNT_INTERNAL and its
 * nested slot finder are the routines that read it:
 *   +0x00  the descriptor template copied into every new volume slot
 *          (`lea (A5),A1` + 18 `move.l`, 0x00E6C32A-0x00E6C33E)
 *   +0x48  log2_table[0..9]: log2 of 1, 2, 4, 8 at those indexes, read as
 *          (0x48,A5,n*2) for the sector-size code (0x00E6C3D8, 0x00E6C46C)
 *          and as (0x4a,A5,mask*2) for the stripe shifts (0x00E6C73C,
 *          0x00E6C748)
 *   +0x5C  pow2_table[0..3]: 1 << code, (0x5c,A5,code*2) (0x00E6C1E8)
 * Image bytes (`gsk read 0x00E826C4 0x64`):
 *   +0x00 00000000 00000000 00000000 00000001  (addr_start = 1)
 *   +0x10 00000000 00000000 00000000 00000000
 *   +0x20 0001 0002 0004 0001 0000 0000 0001 0000
 *   +0x30 .. +0x47 zero
 *   +0x48 0000 0000 0001 0000 0002 0000 0000 0000 0003 0000
 *   +0x5C 0001 0002 0004 0000
 */
#define DISK_$MOUNT_DATA_SIZE   0x64    /* map: DISK_ size = 64 */
typedef struct disk_$mount_data_t {
    disk_$volume_t  vol_template;       /* +0x00 */
    uint16_t        log2_table[10];     /* +0x48 */
    uint16_t        pow2_table[4];      /* +0x5C */
} disk_$mount_data_t;

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(disk_$mount_data_t, log2_table) == 0x48,
               "DISK_$MOUNT_DATA.log2_table at +0x48");
_Static_assert(__builtin_offsetof(disk_$mount_data_t, pow2_table) == 0x5C,
               "DISK_$MOUNT_DATA.pow2_table at +0x5C");
_Static_assert(sizeof(disk_$mount_data_t) == DISK_$MOUNT_DATA_SIZE,
               "DISK_$MOUNT_DATA: map size 0x64");
#endif

MODULE_DATA_DECLARE(disk_$mount_data_t, DISK_$MOUNT_DATA, 0x00E826C4);

/*
 * disk_$diskless_init (0x00E6B6DC, 94 bytes; module-local, the first routine
 * of the `I E6B6DC DISK_` code segment) - on a really diskless node, wire
 * the DBUF and DISK code/data ranges (MST_$WIRE_AREA twice), run DBUF_$INIT
 * and DISK_$INIT, and clear NETWORK_$REALLY_DISKLESS so it happens once.
 * See disk/diskless_init.c.
 */
void disk_$diskless_init(void);

/*
 * disk_$validate_pv_label (0x00E6C21A, 156 bytes; module-local) - read
 * volume `vol_idx`'s PV label (DISK_$GET_BLOCK, block 0, PV_LABEL_$UID) and
 * check it: version <= 1, "APOLLO", a legal interleave for a striped set and
 * a member count of 0, 1, 2, 4 or 8, else status_$invalid_physical_volume_label.
 * Returns the label buffer (A0) - still held, the caller releases it with
 * DBUF_$SET_BUFF - or NIL when the read failed.  See
 * disk/validate_pv_label.c.
 */
disk_$pv_label_t *disk_$validate_pv_label(int16_t vol_idx, status_$t *status);

/*
 * DISK_$PV_MOUNT_INTERNAL (0x00E6C2BC, 1418 bytes) - mount (mount_type 2)
 * or assign (0 / 1) a physical volume, and the other members of a striped
 * set, under MOUNT_LOCK.  Returns (D0) the volume index of the first
 * member.  See disk/pv_mount_internal.c.
 *
 * Frame:
 *   (0x08) mount_type   word: 0 or 1 = assign (DISK_$PV_ASSIGN_N), 2 = mount
 *   (0x0A) unit_type    word: 0, 1 or 4
 *   (0x0C) device       word
 *   (0x0E) unit         word
 *   (0x10) unit_id_ptr  word the driver's dinit fills (its seventh
 *                       argument): copied to the descriptor's unit_id; a
 *                       high byte of 3 forces a remount's re-init
 *   (0x14) num_blocks_ptr, (0x18) sec_per_track_ptr, (0x1C) num_heads_ptr,
 *   (0x20) pvlabel_info  the dinit geometry: +4 receives the label's
 *                       precomp_cyl, +6 is the sectors per block (1, 2, 4,
 *                       8), turned into sector_size_code by log2_table
 *   (0x24) status
 */
int16_t DISK_$PV_MOUNT_INTERNAL(int16_t mount_type, int16_t unit_type,
                                 uint16_t device, uint16_t unit,
                                 uint16_t *unit_id_ptr, uint32_t *num_blocks_ptr,
                                 uint16_t *sec_per_track_ptr, uint16_t *num_heads_ptr,
                                 void *pvlabel_info, status_$t *status);

/*
 * The queue-block pool: up to 16 wired pages of 16 0x40-byte queue blocks,
 * page i (1-based, DMOD_PAGES_ALLOC counts them) at DISK_BLK_POOL_VA +
 * (i - 1) * 0x400.  SAU2 map: `D60C00  DISK_BLK_POOL` (disk_$grow_qblk_pool
 * `movea.l #0xd60c00,A0` 0x00E3BCFC).
 */
#define DISK_BLK_POOL_VA        0x00D60C00
#define DISK_QBLK_SIZE          0x40
#define DISK_QBLKS_PER_PAGE     16
#define DISK_QBLK_MAX_PAGES     16
#define DISK_QBLK_MIN_GROW      0x21    /* first growth's minimum (0x00E3BC52) */

/*
 * disk_$grow_qblk_pool (0x00E3BC40, 586 bytes; module-local) - grow the
 * queue-block pool to hold `count` free blocks (at least 0x21 the first
 * time, when no reserve block exists yet), at most 16 pages in all.  Called
 * by disk_$get_qblks_internal with the module exclusion lock held; drops it
 * around the page allocation.  A Pascal function: D0b is TRUE when the free
 * count now covers the request (`sge` 0x00E3BE7E); the caller ignores it.
 * See disk/grow_qblk_pool.c.
 */
int8_t disk_$grow_qblk_pool(uint16_t count);

/*
 * disk_$get_qblks_internal - Internal queue block allocation body
 *
 * Pascal module body for DISK_$GET_QBLKS. Allocates queue blocks
 * from the disk pool using ML_$EXCLUSION for synchronization.
 * Waits via EC_$WAIT if blocks are unavailable.
 *
 * Uses A5 register as Pascal module data pointer (disk module data).
 *
 * Parameters:
 *   count     - Number of queue blocks to allocate
 *   mode      - Negative for write mode, non-negative for read
 *   first_out - Output: 32-bit VA cell, receives the head of the allocated
 *               block list (0x00E3BF7E `move.l (0xc0,A5),(A0)`)
 *   last_out  - Output: 32-bit VA cell, receives the tail of the allocated
 *               block list (0x00E3BFB8 `move.l (0xc0,A5),(A2)`, reloaded at
 *               0x00E3BFD8 `movea.l (A2),A3`)
 *
 * Both out-cells are four bytes wide on the target; callers that want a host
 * pointer convert with ARCH_VA_TO_PTR.
 *
 * Original address: 0x00e3be8a
 */
void disk_$get_qblks_internal(int16_t count, int8_t mode, uint32_t *first_out,
                              uint32_t *last_out);

/*
 * disk_$rtn_qblks_internal - Return disk queue blocks
 *
 * Puts a chain of queue blocks back on the module free list and wakes the
 * queued read requests the returned blocks can satisfy.
 *
 * Parameters:
 *   count - number of blocks in the chain
 *   first - first block of the chain; when it is DMOD_RESERVE_BLOCK the
 *           write-mode reserve is what is coming back
 *   last  - last block of the chain, whose free_next link is rewritten
 *
 * Original address: 0x00e3c01a
 */
void disk_$rtn_qblks_internal(int16_t count, void *first, void *last);

/*
 * disk_$wait_io - Wait for disk I/O completion
 *
 * Waits on eventcounts for queued I/O operations to complete.
 * Uses EC_$WAIT with 3 eventcounts (two per-process disk ECs plus
 * TIME_$CLOCKH). Iterates 10 disk entries (0x48 spacing) checking
 * DISK_$ERROR_QUE for matching bits in the wait mask.
 *
 * Parameters:
 *   disk_mask      - Bitmask of volumes to check (bits 1-10)
 *   io_wait_val    - Pointer to I/O completion wait value
 *   error_wait_val - Pointer to error wait value (incremented on error detection)
 *
 * Original address: 0x00E3C9FE
 * Size: 188 bytes
 */
void disk_$wait_io(uint16_t disk_mask, int32_t *io_wait_val, int32_t *error_wait_val);

/*
 * AS_IO_SETUP - Setup for async I/O operations
 *
 * Prepares a buffer for asynchronous I/O by wiring it in memory.
 *
 * Parameters:
 *   vol_idx_ptr - Pointer to volume index
 *   buffer      - Buffer address
 *   status      - Receives status code
 *
 * Returns:
 *   Wired buffer address for I/O
 *
 * Original address: TBD
 */
uint32_t AS_IO_SETUP(uint16_t *vol_idx_ptr, uint32_t buffer, status_$t *status);

/*
 * DISK_$ADD_QUE's request array: 32-bit VA cells at DISK_$DATA + 0xB08,
 * entry i (1-based) is the i-th request of the sorted chain; entry 0 and
 * entry count+1 are cleared as terminators (0x00E3C7D2, 0x00E3C8D6).  It
 * runs to the end of the module block, so 34 cells, i.e. at most 32
 * requests per DISK_$ADD_QUE call.
 */
#define DMOD_QUE_ARRAY          0xB08
#define DMOD_QUE_ARRAY_ENTRIES  ((DISK_$DATA_SIZE - DMOD_QUE_ARRAY) / 4)
_Static_assert(DMOD_QUE_ARRAY_ENTRIES == 34, "DISK_$ADD_QUE request array");
#define DISK_$QUE_ARRAY ((uint32_t *)&DISK_$DATA[DMOD_QUE_ARRAY])

/*
 * DISK_$GET_STATS' 22-byte statistics template lives at the end of the
 * device table segment (`D E7AD5C DISK_ size = 198`): 32 * 12 = 0x180 bytes
 * of disk_device_entry_t followed by this block at +0x180 (0x00E3DBBE
 * `lea (0x180,A5),A1` with A5 = 0xE7AD5C).
 */
#define DISK_DEVICES_STATS_OFFSET 0x180
_Static_assert(DISK_MAX_DEVICES * DISK_DEVICE_SIZE == DISK_DEVICES_STATS_OFFSET,
               "DISK_$DEVICES table size");
/* The 0x16-byte template ends at 0x196; the segment's last two bytes are
 * not read by DISK_$GET_STATS (its copy is five longwords and a word). */
_Static_assert(DISK_DEVICES_STATS_OFFSET + DISK_STATS_SIZE <= 0x198,
               "DISK_ device segment is 0x198 bytes");

#endif /* DISK_INTERNAL_H */
