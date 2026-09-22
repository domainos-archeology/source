/*
 * DISK - Disk Subsystem Interface
 *
 * This module provides the disk subsystem interface for Domain/OS.
 * It implements device registration, I/O queuing, and buffer management,
 * delegating to device-specific drivers via jump tables.
 *
 * The disk subsystem maintains:
 * - Volume table with mount state and device info
 * - Device registration table for driver callbacks
 * - I/O queues for asynchronous operations
 * - Buffer cache integration via DBUF module
 */

#ifndef DISK_H
#define DISK_H

#include "base/base.h"
#include "ec/ec.h"
#include "ml/ml.h"

/*
 * Maximum number of volumes and devices
 */
#define DISK_MAX_VOLUMES 64 /* 0x40 */
#define DISK_MAX_DEVICES 32 /* 0x20 */

/*
 * Volume entry size
 */
#define DISK_VOLUME_SIZE 72 /* 0x48 bytes per volume */

/*
 * Device registration entry size
 */
#define DISK_DEVICE_SIZE 12 /* 0x0c bytes per device */

/*
 * Mount states
 */
#define DISK_MOUNT_UNMOUNTED 0
#define DISK_MOUNT_MOUNTED 3

/*
 * Disk lock ID
 */
#define DISK_LOCK_ID 15 /* 0x0f */

/*
 * Status codes
 */
#define status_$disk_write_protected 0x00080007
#define status_$disk_not_ready 0x00080001
#define status_$disk_controller_busy 0x00080002
#define status_$disk_controller_timeout 0x00080003
#define status_$disk_controller_error 0x00080004
#define status_$disk_equipment_check 0x00080005
#define status_$disk_data_check 0x00080009
#define status_$DMA_overrun 0x0008000a
#define status_$logical_volume_not_found 0x00080010
#define status_$disk_block_header_error 0x00080011
#define status_$disk_buffer_not_page_aligned 0x00080013
#define status_$disk_transfer_not_executed 0x00080029 /* "transfer not executed" */
#define status_$disk_already_mounted 0x0008001e
#define status_$memory_parity_error_during_disk_write 0x00080025
#define status_$volume_in_use 0x0008000b
#define status_$volume_table_full 0x0008000c
#define status_$volume_not_properly_mounted 0x0008000d
#define status_$operation_requires_a_physical_volume 0x0008000e
#define status_$invalid_volume_index 0x0008000f
#define status_$invalid_logical_volume_index 0x00080014
#define status_$invalid_unit_number 0x00080018
#define status_$invalid_physical_volume_label 0x0008001a
#define status_$storage_module_stopped 0x0008001b
#define status_$disk_illegal_request_for_device 0x0008002a
#define status_$disk_is_full 0x00010002
#define status_$disk_needs_salvaging 0x00010005

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
 *     DISK_$PV_MOUNT_INTERNAL as `unit` (stored at 0xe6c346) and used by
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
    uint16_t    bat_step;           /* 0x26 (-0x22 / +0xa2): the BAT step, the
                                     *   word DISK_$SORT compares sector
                                     *   distances against (0x00E3C512,
                                     *   0x00E3C55A; == 1 skips its second
                                     *   pass).  Older note follows:
                                     *   word at +0x40 of the logical-volume
                                     *   label.  DISK_$LV_MOUNT stores it into
                                     *   the new LV descriptor (0xe6cbea) and
                                     *   mirrors it into the backing physical
                                     *   volume's descriptor (0xe6cbf4); the
                                     *   only reader is DISK_$GET_MNT_INFO,
                                     *   which reports it at info+0x0c
                                     *   (0xe6bf04).  The DISK subsystem never
                                     *   does arithmetic with it -- this copy
                                     *   exists only so mount info can hand it
                                     *   back to invol.
                                     *
                                     *   The real consumer is the BAT manager,
                                     *   whose own label record already names
                                     *   the same word (bat/bat_internal.h
                                     *   +0x40 bat_step): BAT_$MOUNT defaults
                                     *   it to 3 (bat/mount.c) and
                                     *   BAT_$GET_BAT_STEP returns it.
                                     *
                                     *   AEGIS Internals and Data Structures
                                     *   (Jan 1986) 4.3.3 lists the BAT
                                     *   header's contents in the order the
                                     *   label stores them and ends with "the
                                     *   BAT step to use on this volume"; the
                                     *   glossary: "a bat step of 2 tells the
                                     *   BAT manager to allocate the next block
                                     *   at block n+2.  Users set the bat step,
                                     *   via INVOL, to optimize disk seeks".
                                     *   invol calls it the sector interleave
                                     *   factor -- sys/help/invol.hlp option
                                     *   10, "Display/change sector interleave
                                     *   factor for a logical volume ... Note:
                                     *   Option 10 is not supported at SR10.4",
                                     *   which is why nothing in this release
                                     *   writes it.  The SR10.2 invol binary
                                     *   still carries the dialogue ("Current
                                     *   interleave factor is %ld", "Interval
                                     *   must be 1 on logically addressed
                                     *   disks") and reads it through
                                     *   disk_$get_mnt_info.
                                     *
                                     *   Two neighbours are corroborated by the
                                     *   kernel's own arithmetic: 0xe6cbc8 adds
                                     *   the longwords at label +0x2c and +0x38
                                     *   to get the volume's end address
                                     *   (+0x88).  The word reads 1 in every
                                     *   image checked (sr103.awd and the three
                                     *   disk-images/harddrive SR10.4 images).
                                     */
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
                                     *   BAT_$MOUNT corroborates entry 0's
                                     *   meaning: it compares this word with 1
                                     *   at 0x00E3B83C (cmpi.w #0x1,(-0x12,A2))
                                     *   to decide whether the allocation chunk
                                     *   is scaled by num_parts.
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
_Static_assert(__builtin_offsetof(disk_$volume_t, bat_step) == 0x26,
               "disk_$volume_t.bat_step must be at -0x22 (+0xa2)");
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
 * Descriptor for volume `idx`.  The index multiply is done in 16-bit word
 * arithmetic (lsl.w/add.w), matching the original.
 */
#define DISK_VOL(idx) \
    ((disk_$volume_t *)(DISK_VOLUME_BASE + \
                        (int16_t)((idx) * DISK_VOLUME_SIZE) + \
                        DISK_VOL_DESC_OFFSET))

/*
 * Device registration entry structure (12 bytes per entry)
 * Base address: 0xe7ad5c
 *
 * Layout:
 *   +0x00: Jump table pointer (long)
 *   +0x04: Device type (word)
 *   +0x06: Controller number (word)
 *   +0x08: Unit count (word)
 *   +0x0a: Flags (word)
 */
typedef struct {
  void *jump_table;     /* +0x00: Pointer to device operations */
  uint16_t device_type; /* +0x04: Device type identifier */
  uint16_t controller;  /* +0x06: Controller number */
  uint16_t unit_count;  /* +0x08: Number of units */
  uint16_t flags;       /* +0x0a: Device flags */
} disk_device_entry_t;

/* Layout recovered from the disassembly -- see the field comments above. */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(disk_device_entry_t, jump_table) == 0x00, "disk_device_entry_t.jump_table");
_Static_assert(__builtin_offsetof(disk_device_entry_t, device_type) == 0x04, "disk_device_entry_t.device_type");
_Static_assert(__builtin_offsetof(disk_device_entry_t, controller) == 0x06, "disk_device_entry_t.controller");
_Static_assert(__builtin_offsetof(disk_device_entry_t, unit_count) == 0x08, "disk_device_entry_t.unit_count");
_Static_assert(__builtin_offsetof(disk_device_entry_t, flags) == 0x0A, "disk_device_entry_t.flags");
/* Stride 0x0C, 32 entries: DISK_$REGISTER walks the table with
 * `lea (0xc,A0),A0` / `moveq #0x1f,D1` / `dbf` (00e3d9e8..00e3da0e). */
_Static_assert(sizeof(disk_device_entry_t) == 0x0C, "disk_device_entry_t size");
#endif

/*
 * Device jump table structure
 *
 * Layout:
 *   +0x00: (reserved)
 *   +0x04: (reserved)
 *   +0x08: DINIT - Device initialization
 *   +0x0c: (reserved)
 *   +0x10: DO_IO - Perform I/O operation
 */
typedef struct {
  /* +0x00: spin_down(&entry.controller), a word function; called by
   * DISK_$SPIN_DOWN (0x00E3DB24 - 0x00E3DB30), may be NULL */
  int16_t (*spin_down)(uint16_t *controller_ptr);
  /* +0x04: shutdown(controller, unit); called by DISK_$SHUTDOWN (0xe3dc36) */
  void (*shutdown)(uint16_t controller, uint16_t unit);
  /* +0x08: dinit(unit, controller, vol_idx_ptr, num_blocks_ptr,
   * sec_per_track_ptr, num_heads_ptr, pvlabel_info); called by
   * DISK_$MNT_DINIT (0x00E3DA74 - 0x00E3DA96) */
  void (*dinit)(uint16_t unit, uint16_t controller, void *vol_idx_ptr,
                void *num_blocks_ptr, void *sec_per_track_ptr,
                void *num_heads_ptr, void *pvlabel_info);
  void *_reserved3; /* +0x0c */
  /* +0x10: do_io(vol, req, param_3, result); called by DISK_$DO_IO
   * (0x00E3DAB4) with its own four arguments passed through */
  void (*do_io)(void *vol, void *req, void *param_3, void *result);
  /* +0x14: error_que(vol, is_timeout, result) - a Pascal function
   * returning a word; called by DISK_$ERROR_QUE (0x00E3DAE8) */
  int16_t (*error_que)(void *vol, uint16_t is_timeout, int8_t *result);
  /* +0x18: get_stats(cnum, unit, stats), may be NULL; called by
   * DISK_$GET_STATS (0x00E3DBEE - 0x00E3DBFE) */
  void (*get_stats)(uint16_t cnum, uint16_t unit, void *stats);
} disk_jump_table_t;

/* Layout recovered from the disassembly -- see the field comments above. */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(disk_jump_table_t, spin_down) == 0x00, "disk_jump_table_t.spin_down");
_Static_assert(__builtin_offsetof(disk_jump_table_t, dinit) == 0x08, "disk_jump_table_t.dinit");
_Static_assert(__builtin_offsetof(disk_jump_table_t, _reserved3) == 0x0C, "disk_jump_table_t._reserved3");
_Static_assert(__builtin_offsetof(disk_jump_table_t, do_io) == 0x10, "disk_jump_table_t.do_io");
_Static_assert(__builtin_offsetof(disk_jump_table_t, error_que) == 0x14, "disk_jump_table_t.error_que");
_Static_assert(__builtin_offsetof(disk_jump_table_t, get_stats) == 0x18, "disk_jump_table_t.get_stats");
#endif

/*
 * disk_$per_proc_t - the per-process I/O slot inside DISK_$DATA
 *
 * DISK_$DATA + 0x378 starts an array of 0x1C-byte slots indexed by
 * PROC1_$CURRENT.  DISK_$WRITE_MULTI (0x00E3CCEE) and DISK_$READ_MULTI
 * (0x00E3CFCC) form the address the same way, with the index scaled to
 * 0x1C by "id*32 - id*4" and used as a sign-extended word:
 *
 *   00e3ce2a  move.w (0x00e20608).l,D0w     ; PROC1_$CURRENT
 *   00e3ce30  lsl.w #0x2,D0w / move.w D0w,D4w / neg.w D0w
 *   00e3ce36  lsl.w #0x3,D4w / add.w D4w,D0w ; D0 = id * 0x1C
 *   00e3ce3a  lea (0x0,A5,D0w*0x1),A3
 *   00e3ce3e  move.l (0x378,A3),D5           ; io_ec.value
 *   00e3ce46  move.l (0x384,A3),D4           ; err_ec.value
 *   00e3cd3a  st (0x390,A2)                  ; io_pending = true
 *   00e3ced2  tst.b (0x390,A0) / bmi         ; still pending -> return
 *
 * so the three field offsets 0x378 / 0x384 / 0x390 are 0x00 / 0x0C / 0x18
 * within the slot.  disk/disk_internal.h names the same two eventcounts as
 * DMOD_PER_PROC_IO_EC and DMOD_PER_PROC_ERR_EC.
 *
 * The array runs from DISK_$DATA + 0x378 up to the disk_$error_info_t record
 * at DISK_$DATA + 0xA94, which is 0x1C * 0x41 bytes, so there are 65 slots -
 * index 0 plus the 64 process ids PROC1_$CURRENT can take.
 *
 * The four drivers that retire a request clear the pending byte for the
 * process the REQUEST names (its byte at +0x1E), with the identical five
 * instructions and A1 = 0x00E7A560, i.e. slot base + 0x18 - 4 + 0x1C*id:
 * WIN_$FORMAT_TRACK 0x00E19764, WIN_$DO_IO 0x00E1994A and FLP_FORMAT_TRACK
 * 0x00E3DDB0 / 0x00E3DFBC.  The index is the same 0-based process id, NOT a
 * volume number.
 */
#define DISK_PER_PROC_BASE_OFFSET   0x378
#define DISK_PER_PROC_ENTRIES       0x41    /* ids 0..64 */

typedef struct disk_$per_proc_t {
    ec_$eventcount_t    io_ec;      /* 0x00 (A5+0x378): I/O completion */
    ec_$eventcount_t    err_ec;     /* 0x0C (A5+0x384): error notification */
    boolean             io_pending; /* 0x18 (A5+0x390): set by DISK before it
                                     *       issues the requests, cleared by
                                     *       the driver as each is retired */
    uint8_t             _pad19[3];  /* 0x19: to the 0x1C stride */
} disk_$per_proc_t;

/*
 * ec_$eventcount_t holds two native pointers, so the slot is 0x1C bytes only
 * on the 32-bit target.
 */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(disk_$per_proc_t, io_ec) == 0x00, "disk_$per_proc_t.io_ec");
_Static_assert(__builtin_offsetof(disk_$per_proc_t, err_ec) == 0x0C, "disk_$per_proc_t.err_ec");
_Static_assert(__builtin_offsetof(disk_$per_proc_t, io_pending) == 0x18, "disk_$per_proc_t.io_pending");
_Static_assert(sizeof(disk_$per_proc_t) == 0x1C, "disk_$per_proc_t must be 28 bytes");
#endif

/*
 * 0x00E7A544 == DISK_$DATA (0x00E7A1CC) + 0x378.  On the target the array is
 * at that fixed address; a host build gets a real object so a test can supply
 * it (the same shape the rest of the tree uses for A5-relative tables).
 */
#define DISK_PER_PROC_VA 0x00E7A544
#if defined(ARCH_M68K)
#define DISK_$PER_PROC ((disk_$per_proc_t *)DISK_PER_PROC_VA)
#else
extern disk_$per_proc_t DISK_$PER_PROC[DISK_PER_PROC_ENTRIES];
#endif

/*
 * Global data areas
 */

/*
 * Disk subsystem module data block at 0xe7a1cc.
 *
 * The SAU2 map has `D E7A1CC DISK_ size = B90`, running up to the next
 * segment (`D E7AD5C DISK_ size = 198`, the device table DISK_$DEVICES).  The
 * interior symbols the map names - DISK_$DVTBL (+0xc4), DISK_BPTBL (+0x394),
 * DISK_$ERROR_INFO (+0xa94), DISK_$RAW_PPN (+0xaec), DISK_$NFBLKS (+0xaf8),
 * DISK_$DIAG (+0xafe) and DISK_$DO_CHKSUM (+0xb00) - name cells *inside* this
 * block, so where the tree spells one of them out it does so as an accessor
 * into DISK_$DATA (below), never as storage of its own.
 */
#define DISK_$DATA_SIZE 0xB90
extern uint8_t DISK_$DATA[DISK_$DATA_SIZE];

/* Device registration table at 0xe7ad5c (32 entries, 12 bytes each) */
extern disk_device_entry_t DISK_$DEVICES[];

/* Event counter for disk operations */
extern void *DISK_$EC;

/*
 * Function prototypes - Public API
 */

/* Initialization */
void DISK_$INIT(void);

/* Buffer operations */
/*
 * DISK_$GET_BLOCK (0x00E3BB80) - DBUF_$GET_BLOCK under the disk lock.
 *
 * 0x00E3BB94-0x00E3BBB2 forwards all six arguments verbatim, the two
 * separate WORDS at (0x16,A6) and (0x18,A6) among them, so its frame is
 * DBUF_$GET_BLOCK's frame.  See dbuf/dbuf.h for the two words' meaning.
 */
void *DISK_$GET_BLOCK(int16_t vol_idx, int32_t daddr, void *expected_uid,
                      uint32_t block_hint, uint16_t block_type,
                      uint16_t flags, status_$t *status);
/* DISK_$SET_BUFF (0x00E3BBD4): DBUF_$SET_BUFF under ML lock 15; the third
 * argument is the status cell DBUF_$SET_BUFF fills. */
void DISK_$SET_BUFF(void *buffer, uint16_t flags, status_$t *status);
void DISK_$INVALIDATE(uint16_t vol_idx);

/*
 * disk_$que_t - the elevator queue DISK_$INIT_QUE builds and DISK_$ADD_QUE
 * fills (0x20 bytes).  Layout from DISK_$INIT_QUE (0x00E3C598-0x00E3C5D0):
 *
 *   +0x00 current    cleared (clr.l (A0))
 *   +0x04 position   bit 31 = scan direction (bset.b #7,(0x4,A0)); bits
 *                    4..19 = the current cylinder (andi.l #0xfff0000f
 *                    clears them; ADD_QUE reads (pos & 0xffff0) >> 4)
 *   +0x08 list_a     VA of the head of list A, initially &sentinel_a
 *   +0x0c list_b     VA of the head of list B, initially &sentinel_b
 *   +0x10 sentinel_a { next = 0, daddr high word = 0xFFFF }
 *   +0x18 sentinel_b { next = 0, daddr high word = 0xFFFF }
 *
 * The sentinels are shaped like the first six bytes of a disk_io_req_t
 * (next at +0, cylinder word at +4) so the merge helpers can walk a list
 * without a special case for its end.  The two heads and the sentinel
 * links are 32-bit VA cells, not host pointers.
 */
typedef struct disk_$que_sentinel_t {
    uint32_t    next;           /* +0x00 */
    uint32_t    daddr;          /* +0x04: the cylinder word DISK_$ADD_QUE
                                 *   compares (`cmp.w (0x4,An)`) is the HIGH
                                 *   half, exactly as in disk_io_req_t.daddr;
                                 *   DISK_$INIT_QUE stores 0xFFFF there and
                                 *   leaves the low half (+0x06) alone.
                                 *   Modelled as one longword so a host build
                                 *   reads it the way the m68k does. */
} disk_$que_sentinel_t;

#define DISK_QUE_SENTINEL_CYL   0xFFFF0000u

typedef struct disk_$que_t {
    uint32_t                current;    /* 0x00 */
    uint32_t                position;   /* 0x04 */
    uint32_t                list_a;     /* 0x08 */
    uint32_t                list_b;     /* 0x0c */
    disk_$que_sentinel_t    sentinel_a; /* 0x10 */
    disk_$que_sentinel_t    sentinel_b; /* 0x18 */
} disk_$que_t;

_Static_assert(sizeof(disk_$que_sentinel_t) == 8, "disk_$que_sentinel_t");
_Static_assert(__builtin_offsetof(disk_$que_t, list_a) == 0x08, "disk_$que_t.list_a");
_Static_assert(__builtin_offsetof(disk_$que_t, list_b) == 0x0c, "disk_$que_t.list_b");
_Static_assert(__builtin_offsetof(disk_$que_t, sentinel_a) == 0x10, "disk_$que_t.sentinel_a");
_Static_assert(__builtin_offsetof(disk_$que_t, sentinel_b) == 0x18, "disk_$que_t.sentinel_b");
_Static_assert(sizeof(disk_$que_t) == 0x20, "disk_$que_t must be 0x20 bytes");

#define DISK_QUE_DIRECTION_BIT  0x80000000u   /* bit 31 of disk_$que_t.position */
#define DISK_QUE_POSITION_MASK  0x000ffff0u   /* bits 4..19 */
#define DISK_QUE_POSITION_SHIFT 4

/* Queue operations */
void DISK_$INIT_QUE(void *queue);
/*
 * DISK_$ADD_QUE (0x00E3C716): `dev` is the driver record whose word +0x08
 * bit 9 and word +0x0a (ML_$LOCK id) it reads; `req_list` is the VA of the
 * first disk_io_req_t of the chain.  See disk/add_que.c.
 */
void DISK_$ADD_QUE(uint16_t flags, void *dev, disk_$que_t *queue,
                   void *req_list);
/* DISK_$WAIT_QUE (0x00E3CABA): the exported gate onto disk_$wait_io - sets
 * A5 = DISK_$DATA and forwards the three arguments.  See disk/wait_que.c. */
void DISK_$WAIT_QUE(uint16_t disk_mask, int32_t *io_wait_val, int32_t *error_wait_val);
/* DISK_$ERROR_QUE (0x00E3DAD4): hands back the driver's word result;
 * `result` is a byte cell (bit 7 = error present).  See disk/error_que.c. */
int16_t DISK_$ERROR_QUE(void *vol, uint16_t is_timeout, int8_t *result);
void DISK_$SORT(void *dev_entry, void **queue_ptr);

/*
 * Internal queue block operations (used by AST subsystem)
 * These have different signatures from the public wrappers
 */
/*
 * DISK_$GET_QBLKS (0x00E3BFF4) - allocate a chain of disk queue blocks.
 *
 * Both out-parameters are 32-bit target VA cells, not host pointers: the
 * callee stores them with a single `move.l` each (0x00E3BF7E, 0x00E3BFB8 in
 * disk_$get_qblks_internal) and reloads last_out at that width (0x00E3BFD8).
 * Convert to a host pointer with ARCH_VA_TO_PTR.  Both cells are the same
 * four-byte VA, so both are spelled uint32_t here and in every caller's
 * locals (source-mq3k).
 */
void DISK_$GET_QBLKS(int16_t count, uint32_t *qblk_head, uint32_t *qblk_tail);

void DISK_$RTN_QBLKS(int16_t count, uint32_t qblk_head, uint32_t qblk_tail);

/*
 * DISK_INTERRUPT - disk interrupt dispatcher (m68k vector IO_VECTOR_DISK)
 *
 * Original address: 0x00e0aabc
 */
void DISK_INTERRUPT(void);

/*
 * I/O operations
 *
 * Argument order verified against DISK_$READ (0xe3cf64) and DISK_$WRITE
 * (0xe3cc78): the disk address is argument 2 ((0xa,A6)) and the physical
 * page number argument 3 ((0xe,A6)); DISK_IO receives them the other way
 * round (0xe3cfac / 0xe3ccac).
 */
void DISK_$READ(int16_t vol_idx, uint32_t daddr, uint32_t ppn, uint32_t *info,
                status_$t *status);
void DISK_$WRITE(int16_t vol_idx, uint32_t daddr, uint32_t ppn, uint32_t *info,
                 status_$t *status);

/*
 * Internal multi-block I/O (used by AST subsystem)
 * Takes queue block head/tail from DISK_$GET_QBLKS
 */
void DISK_$READ_MULTI(uint16_t vol_idx, int16_t flags1, int16_t flags2,
                      int32_t qblk_head, uint32_t qblk_tail,
                      int16_t *pages_read, status_$t *status);
void DISK_$WRITE_MULTI(int8_t flags, void *req_list, status_$t *status);
void DISK_$DO_IO(void *dev_entry, void *req, void *param_3, void *result);

/* Allocation operations */
void DISK_$ALLOC_W_HINT(uint16_t vol, uint32_t hint, uint32_t *block,
                        uint32_t count, status_$t *status);

/* Format operations */
void DISK_$FORMAT(uint16_t *vol_idx_ptr, uint16_t *cyl_ptr, uint16_t *head_ptr,
                  status_$t *status);
void DISK_$FORMAT_WHOLE(uint16_t *vol_idx_ptr, status_$t *status);

/* Device management */
uint8_t DISK_$REGISTER(uint16_t *type, uint16_t *controller, uint16_t *units,
                       uint16_t *flags, void **jump_table);
/* DISK_$GET_DRTE (0x00E3DA1C): first DISK_$DEVICES entry with a driver whose
 * device_type / controller match the two words; NULL if none. */
disk_device_entry_t *DISK_$GET_DRTE(uint16_t *ctype_ptr, uint16_t *cnum_ptr);
/* DISK_$MNT_DINIT (0x00E3DA64): calls the driver's dinit slot with `unit`,
 * the entry's controller word and the five pointers.  See disk/mnt_dinit.c. */
void DISK_$MNT_DINIT(uint16_t unit, void **dev_ptr, void *vol_idx_ptr,
                     void *num_blocks_ptr, void *sec_per_track_ptr,
                     void *num_heads_ptr, void *pvlabel_info);
/*
 * DISK_$SHUTDOWN - Shut a disk device down through its driver
 *
 * @param dev_info Device registration entry (jump table at +0x00,
 *                 controller number at +0x06)
 * @param unit     Device unit number
 *
 * Original address: 0x00e3dc28
 */
void DISK_$SHUTDOWN(disk_device_entry_t *dev_info, uint16_t unit);
/* 0x00E3DB04 reads no parameters and returns nothing. */
void DISK_$SPIN_DOWN(void);
/* DISK_$REVALID takes a disk_$volume_t *, so it is declared in
 * disk/disk_internal.h; nothing outside disk/ calls it. */
void DISK_$WRITE_PROTECT(int16_t mode, int16_t vol_idx, status_$t *status);
/*
 * DISK_$GET_STATS (0x00E3DB9C) takes FIVE arguments, not four: its frame is
 * (0x8,A6) word, (0xA,A6) word, (0xC,A6) word, (0xE,A6) long, (0x12,A6) long
 * and every caller cleans up 0x10 bytes (2 result + 2 + 2 + 2 + 4 + 4).  The
 * first two words are matched against a controller table entry's +0x04 and
 * +0x06, i.e. dcte_t.ctype and dcte_t.cnum (0x00E3DBE0 / 0x00E3DBE6); the
 * third is handed to the driver's own statistics routine (0x00E3DBF6).
 * The stats buffer is 22 bytes: DISK_$GET_STATS itself preloads it with five
 * longwords and a word from A5+0x180 (0x00E3DBC4-0x00E3DBCE).
 *
 * ASKNODE_$INTERNET_INFO calls it three ways: (0,0,0) for the summary
 * (0x00E647FA), (0,*param,*(param+2)) for one drive (0x00E6484A) and
 * (4,0,unit) for each of four units (0x00E6491C).
 */
#define DISK_STATS_SIZE 0x16
void DISK_$GET_STATS(int16_t ctype, int16_t cnum, int16_t unit,
                     uint8_t *has_stats, void *stats);
void DISK_$UNASSIGN(uint16_t *vol_idx_ptr, status_$t *status);
void DISK_$UNASSIGN_ALL(void);
void DISK_$REVALIDATE(int16_t vol_idx);
void DISK_$DISMOUNT(uint16_t vol_idx);
void DISK_$GET_ERROR_INFO(void *buffer);
void DISK_$LVUID_TO_VOLX(void *uid_ptr, int16_t *vol_idx, status_$t *status);

/* Volume assignment operations */
/* DISK_$PV_MOUNT (0x00E6C9E8): a procedure whose D0 is whatever
 * DISK_$PV_MOUNT_INTERNAL(2, unit_type, device, unit, ...) left; VOLX reads
 * it as the volume index.  See disk/pv_mount.c. */
int16_t DISK_$PV_MOUNT(int16_t unit_type, int16_t device, int16_t unit,
                       status_$t *status);
int16_t DISK_$LV_MOUNT(uid_t *lv_uid, status_$t *status_ret);
void DISK_$LV_UID(int16_t vol_idx, int16_t lv_num, uid_t *uid_ret,
                  status_$t *status);
void DISK_$PV_ASSIGN_N(int16_t *unit_type_ptr, int16_t *device_ptr,
                       int16_t *unit_ptr, uint16_t *flags_ptr,
                       uint16_t *vol_idx_ptr, uint32_t *num_blocks_ptr,
                       uint16_t *sec_per_track_ptr, uint16_t *num_heads_ptr,
                       uint32_t *pvlabel_info, status_$t *status);
/* DISK_$PV_ASSIGN (0x00E6C95C): eight arguments; info_ptr doubles as the
 * num_blocks cell handed to DISK_$PV_ASSIGN_N.  See disk/pv_assign.c. */
void DISK_$PV_ASSIGN(int16_t *unit_type_ptr, int16_t *device_ptr,
                     int16_t *unit_ptr, uint16_t *vol_idx_ptr,
                     int32_t *info_ptr, uint16_t *sec_per_track_ptr,
                     uint16_t *num_heads_ptr, status_$t *status);
uint16_t DISK_$LV_ASSIGN(uint16_t *vol_idx_ptr, uint16_t *lv_idx_ptr,
                         int32_t *blocks_avail_ptr, status_$t *status);

/* Async I/O operations */
/* Argument 3 of both is the caller's page-aligned buffer VA by value
 * (0x00E6B87C / 0x00E6B906 `move.l (0x10,A6),-(SP)`); info is the
 * eight-longword block header (out for a read, in for a write). */
void DISK_$AS_READ(uint16_t *vol_idx_ptr, uint32_t *daddr_ptr, uint32_t buffer,
                   uint32_t *info, status_$t *status);
void DISK_$AS_WRITE(uint16_t *vol_idx_ptr, uint32_t *daddr_ptr, uint32_t buffer,
                    uint32_t *info, status_$t *status);
void DISK_$AS_XFER_MULTI(uint16_t *vol_idx_ptr, int16_t *count_ptr,
                         int16_t *op_type_ptr, uint32_t *daddr_array,
                         uint32_t **info_array, uint32_t *buffer_array,
                         uint32_t *status_array, status_$t *status);
void DISK_$AS_OPTIONS(uint16_t *vol_idx_ptr, uint16_t *options_ptr,
                      status_$t *status);

/* Diagnostic and manufacturing operations */
void DISK_$DIAG_IO(int16_t *op_ptr, uint16_t *vol_idx_ptr, uint32_t *daddr_ptr,
                   uint32_t buffer, uint32_t *info, status_$t *status);
/* DISK_$READ_MFG_BADSPOTS (0x00E6B7E4): daddr by reference, the page VA by
 * value (0x00E6B804 `pea (A3)` pushes the value of (0x10,A6)). */
void DISK_$READ_MFG_BADSPOTS(uint16_t *vol_idx_ptr, uint32_t *daddr_ptr,
                             uint32_t buffer, status_$t *status);
/*
 * disk_$mnt_info_t - the record DISK_$GET_MNT_INFO fills in (disk/get_mnt_info.c
 * documents the field origins).  ASKNODE_$INTERNET_INFO passes 0x2A as its
 * size (the constant cell at 0x00E658BA) and reads dev_type, unit_id and the
 * flags byte back out (0x00E64D00-0x00E64DC4).
 */
typedef struct disk_$mnt_info_t {
    uint32_t vol_start;         /* 0x00: volume address range start */
    uint32_t vol_end;           /* 0x04: volume address range end */
    uint16_t dev_type;          /* 0x08: device type (dev_info +4) */
    uint16_t unit_id;           /* 0x0A: unit id (descriptor +0x9A) */
    uint16_t bat_step;          /* 0x0C: descriptor +0xA2 */
    uint16_t sectors_per_track; /* 0x0E: descriptor +0x9C, first word */
    uint16_t heads;             /* 0x10: descriptor +0x9E */
    uint16_t sectors_per_block; /* 0x12: 1 << sector_size_code */
    uint16_t n_partitions;      /* 0x14 */
    uint16_t part_info[8];      /* 0x16..0x25 */
    uint16_t interleave;        /* 0x26: descriptor +0xB2 */
    /*
     * 0x28 is a BYTE everywhere: DISK_$GET_MNT_INFO sets and clears bit 6 of
     * it with bset.b/bclr.b (0x00E6BEBC / 0x00E6BECA) and
     * ASKNODE_$INTERNET_INFO tests the enclosing word's bit 15 and bit 14
     * (0x00E64D00 "tst.w"; 0x00E64DA4 "btst.l #0xE"), which are bits 7 and 6
     * of this byte.
     */
    uint8_t  flags;             /* 0x28 */
    uint8_t  _pad_29;           /* 0x29 */
} __attribute__((packed)) disk_$mnt_info_t;

#define DISK_MNT_FLAG_LOGICAL_VOLUME 0x40   /* bit 6 (0x00E6BEBC) */

/* Pointer-free, so the layout holds on every host. */
_Static_assert(__builtin_offsetof(disk_$mnt_info_t, dev_type)   == 0x08, "disk_$mnt_info_t.dev_type");
_Static_assert(__builtin_offsetof(disk_$mnt_info_t, unit_id)    == 0x0A, "disk_$mnt_info_t.unit_id");
_Static_assert(__builtin_offsetof(disk_$mnt_info_t, part_info)  == 0x16, "disk_$mnt_info_t.part_info");
_Static_assert(__builtin_offsetof(disk_$mnt_info_t, interleave) == 0x26, "disk_$mnt_info_t.interleave");
_Static_assert(__builtin_offsetof(disk_$mnt_info_t, flags)      == 0x28, "disk_$mnt_info_t.flags");
_Static_assert(sizeof(disk_$mnt_info_t) == 0x2A, "disk_$mnt_info_t must be 0x2A bytes");

/* param_2 is the record size the caller declares - ASKNODE passes 0x2A -
 * and is never read (DISK_$GET_MNT_INFO 0x00E6BE4A touches (0x8,A6),
 * (0x10,A6) and (0x14,A6) only). */
/* `info` is a disk_$mnt_info_t; it stays void * here because ASKNODE's
 * host test mocks this prototype. */
void DISK_$GET_MNT_INFO(uint16_t *vol_idx_ptr, void *param_2, void *info,
                        status_$t *status);

/*
 * DISK_$DO_CHKSUM - Disk checksum enable flag (negative = checksums on)
 *
 * Read/temporarily cleared by PMAP_$PURIFIER_L and pmap_$fill_write_qblks.
 *
 * SAU2 map: `E7ACCC  DISK_$DO_CHKSUM`, i.e. DISK_$DATA + 0xB00 - one byte of
 * the DISK_ module block, not an object of its own.
 *
 * Original address: 0xE7ACCC (1 byte)
 */
#define DISK_$DO_CHKSUM_OFFSET  0xB00  /* 0x00E7ACCC - 0x00E7A1CC */
_Static_assert(DISK_$DO_CHKSUM_OFFSET < DISK_$DATA_SIZE,
               "DISK_$DO_CHKSUM must lie inside DISK_$DATA");
#define DISK_$DO_CHKSUM (*(int8_t *)&DISK_$DATA[DISK_$DO_CHKSUM_OFFSET])

/*
 * DISK_$DIAG - Diagnostic mode flag
 *
 * Enables the privileged diagnostic paths: DISK_$DIAG_IO tests it as a
 * Domain boolean (`< 0`), while STOP_$WATCH's peek/poke branch table gates
 * the three poke operations on it being merely non-zero
 * (0x00E8186A: `tst.b (0x00e7acca).l / bne`).
 *
 * SAU2 map: `E7ACCA  DISK_$DIAG`, i.e. DISK_$DATA + 0xAFE - one byte of the
 * DISK_ module block, not an object of its own.
 *
 * Original address: 0xE7ACCA (1 byte)
 */
#define DISK_$DIAG_OFFSET       0xAFE  /* 0x00E7ACCA - 0x00E7A1CC */
_Static_assert(DISK_$DIAG_OFFSET < DISK_$DATA_SIZE,
               "DISK_$DIAG must lie inside DISK_$DATA");
#define DISK_$DIAG (*(int8_t *)&DISK_$DATA[DISK_$DIAG_OFFSET])

#endif /* DISK_H */
