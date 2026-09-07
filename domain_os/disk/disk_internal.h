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

/*
 * Per-volume disk descriptor (0x48 bytes)
 *
 * The disk module data area starts at DISK_$DATA (0xe7a1cc, DISK_VOLUME_BASE);
 * the m68k code loads A5 with this address.  Each descriptor is
 * DISK_VOLUME_SIZE (0x48) bytes and the machine code reaches the one for
 * volume N in one of two equivalent ways:
 *
 *   (0xe7a1cc + N*0x48) + positive offset      e.g. DISK_$READ, DISK_$DIAG_IO
 *   (0xe7a290 + N*0x48) - negative offset      e.g. DISK_$LV_ASSIGN, DISK_$DISMOUNT
 *
 * 0xe7a290 - 0xe7a1cc == 0xc4, so both name the same bytes.  The descriptor
 * proper runs from -0x48 to 0 in the second form, i.e. from +0x7c to +0xc4 in
 * the first: DISK_$PV_MOUNT_INTERNAL copies a whole 0x48-byte template into it
 * with `lea (-0x48,A4),A3` + 18 `move.l` (0xe6c332-0xe6c33e), and
 * disk_$wait_io hands the same address to DISK_$ERROR_QUE as `pea (0x7c,A2)`
 * (0xe3ca8a).  Volume index 0 is reserved: its descriptor would overlap the
 * module's exclusion lock and free-list pointers.
 *
 * Both offset forms are given for every field below.  Verified against
 * DISK_$READ (0xe3cf64), DISK_IO (0xe3d50e), DISK_$WRITE_PROTECT (0xe3d956),
 * DISK_$AS_OPTIONS (0xe6c0a8), DISK_$PV_MOUNT_INTERNAL (0xe6c2bc),
 * DISK_$GET_MNT_INFO (0xe6be4a), DISK_$LV_ASSIGN (0xe6cdb2),
 * DISK_$DISMOUNT (0xe6cfea) and DISK_$LVUID_TO_VOLX (0xe6d134).
 *
 * NOTE: the two "unit" fields the old macros disagreed about are distinct.
 *   dev_unit (+0x98, -0x2c) is the device unit number passed to
 *     DISK_$PV_MOUNT_INTERNAL as `unit_lo` (stored at 0xe6c346) and used by
 *     DISK_$DISMOUNT (0xe6d084) and DISK_$LV_ASSIGN (0xe6cf32) to recognise
 *     descriptors that share one physical drive.
 *   unit_id (+0x9a, -0x2a) is stored from *vol_idx_ptr (0xe6c39c) or from the
 *     PV label word at +0x32 (0xe6c438) and is what DISK_$GET_MNT_INFO reports
 *     at info+0x0a (0xe6befe).
 */
#define DISK_VOLUME_BASE          ((uint8_t *)0x00e7a1cc)

/* Byte offset of the descriptor within DISK_VOLUME_BASE + N * 0x48 */
#define DISK_VOL_DESC_OFFSET      0x7c

typedef struct disk_$volume_t {
    uid_t       lv_uid;             /* 0x00 (-0x48 / +0x7c): logical volume UID
                                     *   (DISK_$LVUID_TO_VOLX 0xe6d17c) */
    uint32_t    lv_start;           /* 0x08 (-0x40 / +0x84): LV start block;
                                     *   0 for a physical volume descriptor */
    uint32_t    addr_start;         /* 0x0c (-0x3c / +0x88): lowest legal disk
                                     *   address (DISK_$DIAG_IO); LV_ASSIGN
                                     *   stores the LV size here (0xe6cf9a) */
    uint32_t    addr_end;           /* 0x10 (-0x38 / +0x8c): highest legal
                                     *   disk address */
    uint16_t    mount_state;        /* 0x14 (-0x34 / +0x90) */
    int16_t     mount_proc;         /* 0x16 (-0x32 / +0x92): owning PID */
    void       *dev_info;           /* 0x18 (-0x30 / +0x94): device descriptor */
    uint16_t    dev_unit;           /* 0x1c (-0x2c / +0x98): device unit number */
    uint16_t    unit_id;            /* 0x1e (-0x2a / +0x9a): unit id reported by
                                     *   DISK_$GET_MNT_INFO */
    uint16_t    sec_per_track;      /* 0x20 (-0x28 / +0x9c) */
    uint16_t    num_heads;          /* 0x22 (-0x26 / +0x9e) */
    uint16_t    blocks_per_cyl;     /* 0x24 (-0x24 / +0xa0): disk blocks in one
                                     *   cylinder.  DISK_$PV_MOUNT_INTERNAL
                                     *   computes it as
                                     *   (num_heads * sec_per_track) >>
                                     *   sector_size_code
                                     *   (0xe6c74e-0xe6c75c) and both
                                     *   disk_$map_request (0xe3cb84,
                                     *   0xe3cc08) and disk_$io_error
                                     *   (0xe3c1d0, 0xe3c1ee) use it as the
                                     *   divisor/multiplier that turns a disk
                                     *   address into a cylinder number. */
    uint16_t    lv_shift;           /* 0x26 (-0x22 / +0xa2): the word at +0x40
                                     *   of the logical-volume label.
                                     *   DISK_$LV_MOUNT stores it into the new
                                     *   LV descriptor (0xe6cbea) and then
                                     *   mirrors it into the backing physical
                                     *   volume's descriptor (0xe6cbf4); the
                                     *   only reader is DISK_$GET_MNT_INFO,
                                     *   which reports it at info+0x0c
                                     *   (0xe6bf04).  Nothing in the kernel
                                     *   does arithmetic with it, so its
                                     *   meaning is not established by the
                                     *   code -- disk/lv_mount.c calls the
                                     *   label word a "shift value".
                                     *   TODO(source-zot4): confirm what the
                                     *   LV label's +0x40 word means. */
    uint16_t    as_options;         /* 0x28 (-0x20 / +0xa4): async I/O options.
                                     *   Written as a whole word by
                                     *   DISK_$AS_OPTIONS (0xe6c108) and cleared
                                     *   by LV_ASSIGN (0xe6cfa8); its low byte
                                     *   (+0xa5) holds the DISK_VOL_FLAG_* bits
                                     *   that DISK_$WRITE_PROTECT (0xe3d98c) and
                                     *   DISK_IO (0xe3d584) poke with bset.b /
                                     *   btst.b.  On big-endian m68k bit n of
                                     *   that byte is bit n of this word. */
    uint16_t    sector_size_code;   /* 0x2a (-0x1e / +0xa6): log2 of the number
                                     *   of hardware sectors in one disk block.
                                     *   It is used as a shift in both
                                     *   directions: PV_MOUNT_INTERNAL divides
                                     *   heads*sectors by it to get
                                     *   blocks_per_cyl (0xe6c75a),
                                     *   disk_$map_request shifts a block
                                     *   remainder left by it before splitting
                                     *   into head/sector (0xe3cba2) and
                                     *   disk_$io_error shifts the other way
                                     *   (0xe3c1c8).  DISK_$GET_MNT_INFO
                                     *   reports 1 << code at info+0x12
                                     *   (0xe6bf10-0xe6bf34). */
    uint16_t    num_parts;          /* 0x2c (-0x1c / +0xa8): partition count;
                                     *   DISK_$DISMOUNT reads it as a unit count */
    /*
     * The four striping parameters.  They only matter when part_volx[0] (the
     * interleave mode, +0xb2) is non-zero; DISK_$PV_MOUNT_INTERNAL derives
     * all four from the physical volume label at 0xe6c6ec-0xe6c74c and
     * disk_$map_request (0xe3cbc8-0xe3cc30) and disk_$io_error
     * (0xe3c1b2-0xe3c20e) are the only users.
     *
     * A striped disk address is split as
     *
     *   chunk_offset = daddr & stripe_blk_mask          (0xe3cbd0)
     *   group        = daddr >> stripe_blk_shift        (0xe3cbd4)
     *   cyl_quot     = group / blocks_per_cyl           (0xe3cc08)
     *   member       = cyl_quot & stripe_vol_mask       (0xe3cc0e)
     *   cylinder     = cyl_quot >> stripe_vol_shift     (0xe3cc1e)
     *   volume       = part_volx[chunk_offset +
     *                            (member << stripe_blk_shift) + 1]
     *
     * so the two masks are one less than a power of two and the two shifts
     * are the matching log2: PV_MOUNT_INTERNAL looks each shift up in the
     * word table at DISK module base + 0x4a indexed by its mask
     * (0xe6c73c, 0xe6c748).
     */
    uint16_t    stripe_blk_mask;    /* 0x2e (-0x1a / +0xaa): (blocks per stripe
                                     *   chunk) - 1.  Also stored, plus one,
                                     *   into the request at +0x1c by
                                     *   disk_$map_request (0xe3cb68). */
    uint16_t    stripe_blk_shift;   /* 0x30 (-0x18 / +0xac): log2 of the blocks
                                     *   per stripe chunk */
    uint16_t    stripe_vol_mask;    /* 0x32 (-0x16 / +0xae): (number of striped
                                     *   volumes) - 1 */
    uint16_t    stripe_vol_shift;   /* 0x34 (-0x14 / +0xb0): log2 of the number
                                     *   of striped volumes */
    uint16_t    part_volx[9];       /* 0x36 (-0x12 / +0xb2): partition -> volume
                                     *   index table.  DISK_$FORMAT indexes it
                                     *   as (+0xb2)[part] for part 1..8
                                     *   (0xe3d46c) and DISK_$GET_MNT_INFO reads
                                     *   entry 1 (+0xb4) as the physical volume
                                     *   backing an LV (0xe6bec2).  Entry 0
                                     *   (+0xb2) is copied to info+0x26
                                     *   (0xe6bf40).
                                     *
                                     *   Entry 0 is NOT a volume index: it is
                                     *   the interleave mode, taken from the PV
                                     *   label word at +0xbc (0xe6c4f4,
                                     *   0xe6c6e6).  A non-zero value is what
                                     *   makes disk_$map_request take the
                                     *   striped path (0xe3cb72), and
                                     *   PV_MOUNT_INTERNAL switches on it at
                                     *   0xe6c6f2 to set the stripe_* fields:
                                     *     1  blk_mask := num_parts - 1
                                     *     2  vol_mask := num_parts - 1
                                     *     3  neither (the template's values)
                                     *     4  blk_mask := 1,
                                     *        vol_mask := (num_parts >> 1) - 1
                                     *     5  blk_mask := 3,
                                     *        vol_mask := (num_parts >> 2) - 1
                                     *   Entries 1..8 come from the label at
                                     *   +0xac..+0xbb (0xe6c4e0, 0xe6c6d2). */
} disk_$volume_t;

#if defined(ARCH_M68K)
_Static_assert(sizeof(disk_$volume_t) == DISK_VOLUME_SIZE,
               "disk_$volume_t must be 0x48 bytes");
_Static_assert(__builtin_offsetof(disk_$volume_t, lv_start) == 0x08,
               "disk_$volume_t.lv_start must be at -0x40 (+0x84)");
_Static_assert(__builtin_offsetof(disk_$volume_t, addr_start) == 0x0c,
               "disk_$volume_t.addr_start must be at -0x3c (+0x88)");
_Static_assert(__builtin_offsetof(disk_$volume_t, addr_end) == 0x10,
               "disk_$volume_t.addr_end must be at -0x38 (+0x8c)");
_Static_assert(__builtin_offsetof(disk_$volume_t, mount_state) == 0x14,
               "disk_$volume_t.mount_state must be at -0x34 (+0x90)");
_Static_assert(__builtin_offsetof(disk_$volume_t, mount_proc) == 0x16,
               "disk_$volume_t.mount_proc must be at -0x32 (+0x92)");
_Static_assert(__builtin_offsetof(disk_$volume_t, dev_info) == 0x18,
               "disk_$volume_t.dev_info must be at -0x30 (+0x94)");
_Static_assert(__builtin_offsetof(disk_$volume_t, dev_unit) == 0x1c,
               "disk_$volume_t.dev_unit must be at -0x2c (+0x98)");
_Static_assert(__builtin_offsetof(disk_$volume_t, unit_id) == 0x1e,
               "disk_$volume_t.unit_id must be at -0x2a (+0x9a)");
_Static_assert(__builtin_offsetof(disk_$volume_t, sec_per_track) == 0x20,
               "disk_$volume_t.sec_per_track must be at -0x28 (+0x9c)");
_Static_assert(__builtin_offsetof(disk_$volume_t, as_options) == 0x28,
               "disk_$volume_t.as_options must be at -0x20 (+0xa4)");
_Static_assert(__builtin_offsetof(disk_$volume_t, sector_size_code) == 0x2a,
               "disk_$volume_t.sector_size_code must be at -0x1e (+0xa6)");
_Static_assert(__builtin_offsetof(disk_$volume_t, num_parts) == 0x2c,
               "disk_$volume_t.num_parts must be at -0x1c (+0xa8)");
_Static_assert(__builtin_offsetof(disk_$volume_t, num_heads) == 0x22,
               "disk_$volume_t.num_heads must be at -0x26 (+0x9e)");
_Static_assert(__builtin_offsetof(disk_$volume_t, blocks_per_cyl) == 0x24,
               "disk_$volume_t.blocks_per_cyl must be at -0x24 (+0xa0)");
_Static_assert(__builtin_offsetof(disk_$volume_t, lv_shift) == 0x26,
               "disk_$volume_t.lv_shift must be at -0x22 (+0xa2)");
_Static_assert(__builtin_offsetof(disk_$volume_t, stripe_blk_mask) == 0x2e,
               "disk_$volume_t.stripe_blk_mask must be at -0x1a (+0xaa)");
_Static_assert(__builtin_offsetof(disk_$volume_t, stripe_blk_shift) == 0x30,
               "disk_$volume_t.stripe_blk_shift must be at -0x18 (+0xac)");
_Static_assert(__builtin_offsetof(disk_$volume_t, stripe_vol_mask) == 0x32,
               "disk_$volume_t.stripe_vol_mask must be at -0x16 (+0xae)");
_Static_assert(__builtin_offsetof(disk_$volume_t, stripe_vol_shift) == 0x34,
               "disk_$volume_t.stripe_vol_shift must be at -0x14 (+0xb0)");
_Static_assert(__builtin_offsetof(disk_$volume_t, part_volx) == 0x36,
               "disk_$volume_t.part_volx must be at -0x12 (+0xb2)");
#endif

/*
 * Bits of the low byte of disk_$volume_t.as_options (+0xa5, -0x1f).
 * These are addressed with byte bit instructions in the original; on
 * big-endian m68k the low byte's bit n is bit n of the containing word.
 */
#define DISK_VOL_FLAG_WRITE_PROTECT  0x0001  /* 0xe3d98c bset.b #0,(0xa5,A1) */
#define DISK_VOL_FLAG_NO_HDR_CHECK   0x0004  /* 0xe3d5a0 btst.b #2,(0xa5,A4) */

/*
 * Descriptor for volume `idx`.  The index multiply is done in 16-bit word
 * arithmetic (lsl.w/add.w), matching the original.
 */
#define DISK_VOL(idx) \
    ((disk_$volume_t *)(DISK_VOLUME_BASE + \
                        (int16_t)((idx) * DISK_VOLUME_SIZE) + \
                        DISK_VOL_DESC_OFFSET))

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
#define DMOD_RESERVE_BLOCK    0x0BC  /* void* - reserve block for write-mode allocation */
#define DMOD_FREE_HEAD        0x0C0  /* void* - free list head */
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
#define DISK_QBLK_FORWARD     0x00  /* void* - next in allocated chain */
#define DISK_QBLK_FREE_NEXT   0x08  /* void* - next in free list */
#define DISK_QBLK_STATUS      0x0C  /* uint32_t - I/O status */
#define DISK_QBLK_FLAGS       0x1C  /* uint16_t - I/O flags */
#define DISK_QBLK_OWNER       0x1E  /* uint8_t - owning process ID */
#define DISK_QBLK_RESERVED    0x1F  /* uint8_t - reserved */

/*
 * Internal data structures
 */

/* Diagnostic mode flag - enables special diagnostic I/O operations */
extern int8_t DISK_$DIAG;

/* Mount lock - protects mount table operations */
extern ml_$exclusion_t MOUNT_LOCK;

/* Exclusion locks for disk operations (at specific offsets from DISK_$DATA) */
extern ml_$exclusion_t ml_$exclusion_t_00e7a274;  /* DISK_$DATA +0xa8 */
extern ml_$exclusion_t ml_$exclusion_t_00e7a25c;  /* DISK_$DATA +0x90 */

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
    struct disk_io_req_t *next;     /* 0x00: next block in the allocated chain */
    uint32_t    daddr;              /* 0x04: disk address / (head, sector) */
    struct disk_io_req_t *free_next;/* 0x08: next block in the free list */
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

#if defined(ARCH_M68K)
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
#endif

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
 * DISK_$PV_MOUNT_INTERNAL - Internal physical volume mount
 *
 * Core implementation for PV_ASSIGN_N. Handles device initialization,
 * volume table setup, and multi-volume configurations.
 *
 * Parameters:
 *   mount_type     - 0=normal, 1=boot, 2=mount, 4=remount
 *   device_num     - Device number
 *   unit_hi        - Unit high byte
 *   unit_lo        - Unit low byte (device unit)
 *   vol_idx_ptr    - Output: volume index assigned
 *   num_blocks_ptr - I/O: number of blocks
 *   sec_per_track_ptr - I/O: sectors per track
 *   num_heads_ptr  - I/O: number of heads
 *   pvlabel_info   - I/O: PV label info (16 bytes)
 *   status         - Output: status code
 *
 * Returns:
 *   Volume index assigned
 *
 * Original address: 0x00e6c2bc
 */
int16_t DISK_$PV_MOUNT_INTERNAL(int16_t mount_type, int16_t device_num,
                                 uint16_t unit_hi, uint16_t unit_lo,
                                 uint16_t *vol_idx_ptr, uint32_t *num_blocks_ptr,
                                 uint16_t *sec_per_track_ptr, uint16_t *num_heads_ptr,
                                 void *pvlabel_info, status_$t *status);

/*
 * disk_$grow_qblk_pool - Grow the disk queue block pool
 *
 * Allocates physical pages and initializes new queue blocks in the
 * disk module's free pool. Called under the module exclusion lock
 * when allocation requests cannot be satisfied from the current pool.
 *
 * Parameters:
 *   count - Requested number of blocks (used to calculate pages needed)
 *
 * Original address: 0x00e3bc40
 * Size: 586 bytes
 */
void disk_$grow_qblk_pool(int16_t count);

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
 *   first_out - Output: pointer to head of allocated block list
 *   last_out  - Output: pointer to tail of allocated block list
 *
 * Original address: 0x00e3be8a
 */
void disk_$get_qblks_internal(int16_t count, int8_t mode, void *first_out, void *last_out);

/*
 * disk_$rtn_qblks_internal - Return disk queue blocks
 *
 * Internal function to return previously allocated queue blocks.
 *
 * Parameters:
 *   vol_idx - Volume index
 *   blocks  - Pointer to blocks to return
 *   param_3 - Additional parameter
 *
 * Original address: 0x00e3c01a
 */
void disk_$rtn_qblks_internal(int16_t vol_idx, void *blocks, void *param_3);

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

#endif /* DISK_INTERNAL_H */
