/*
 * WIN - Winchester Disk Driver
 *
 * This module provides the Winchester (hard disk) driver for Domain/OS.
 * It implements the device-specific operations for Winchester disk
 * controllers using the ANSI standard command set.
 *
 * The WIN module:
 * - Registers with the DISK subsystem via a jump table
 * - Handles read/write/format operations
 * - Manages seek operations and error recovery
 * - Provides interrupt handling for async I/O
 */

#ifndef WIN_H
#define WIN_H

#include "base/base.h"
#include "ml/ml.h"
#include "ec/ec.h"
#include "disk/disk.h"
#include "parity/parity.h"
#include "misc/crash_system.h"
#include "io/io.h"   /* status_$io_controller_not_in_system */

/*
 * WIN data area base at 0xe2b89c.
 *
 * Every WIN entry point loads it with `lea (0xe2b89c).l,A5` (e.g. DISK_INIT
 * at 0x00E1998E, WIN_$DINIT at 0x00E19CF0) and then addresses the module
 * globals as (off,A5).  The translated code keeps that byte-offset view
 * (WIN_DATA_BASE + off, the offsets below).
 *
 * WIN_$DATA is the WIN_ data segment (SAU2 map "D E2B89C WIN_ size = 78",
 * interior symbol WIN_$CNT at +0x40) as a MODULE_DATA block (source-702z;
 * the target had an absolute-address macro, the host a test array).  The
 * `image' arm spells the cells the image ships non-zero (`gsk read 0xE2B89C
 * 0x78'): the driver entry table at +0x10 that WIN_$CINIT registers with
 * DISK_$REGISTER, laid out as disk_jump_table_t is on the target but held
 * as 32-bit code VAs so the layout is the same on every build, the longword
 * 1 at +0x68 and the current head / cylinder at +0x72 / +0x74 (-1, "unknown",
 * as win_$reinit_drive resets them).
 */
#define WIN_DATA_SIZE 0x78

typedef union win_$data_t {
    uint8_t bytes[WIN_DATA_SIZE];       /* the (off,A5) view */
    struct {
        uint8_t  _0000[0x10];
        uint32_t jump_table[8];         /* +0x10: disk_jump_table_t (code VAs) */
        uint8_t  _0030[0x38];
        uint32_t _unknown_68;           /* +0x68: 1 in the image */
        uint8_t  _006c[0x06];
        int16_t  cur_head;              /* +0x72: -1 in the image */
        int16_t  cur_cyl;               /* +0x74: -1 in the image */
        uint8_t  _0076[0x02];
    } image;
} win_$data_t;

_Static_assert(sizeof(win_$data_t) == WIN_DATA_SIZE, "WIN_: map size 0x78");
_Static_assert(offsetof(win_$data_t, image.jump_table) == 0x10, "WIN jump table");
_Static_assert(offsetof(win_$data_t, image._unknown_68) == 0x68, "WIN +0x68");
_Static_assert(offsetof(win_$data_t, image.cur_head) == 0x72, "WIN current head");
_Static_assert(offsetof(win_$data_t, image.cur_cyl) == 0x74, "WIN current cylinder");

MODULE_DATA_DECLARE(win_$data_t, WIN_$DATA, 0x00E2B89C);
#define WIN_DATA_BASE (WIN_$DATA.bytes)

/*
 * WIN data area structure
 *
 * Layout:
 *   +0x00: Controller info pointer
 *   +0x04: Base address
 *   +0x08: Device type
 *   +0x0a: Flags
 *   +0x0c: Lock ID per unit (array, 0x0c bytes each)
 *   +0x30: Event counter (per unit)
 *   +0x40: Statistics counters (WIN_$CNT)
 *   +0x58: Current status
 *   +0x5c: Current device info
 *   +0x60: Current request pointer
 *   +0x6c: Extended status byte
 *   +0x6e: Last disk status word
 *   +0x74: Current cylinder
 *   +0x76: Flag byte
 */

/* Offsets in WIN data area */
#define WIN_CTRL_INFO_OFFSET 0x00
#define WIN_BASE_ADDR_OFFSET 0x04
#define WIN_DEV_TYPE_OFFSET 0x08
#define WIN_FLAGS_OFFSET 0x0a
#define WIN_LOCK_ARRAY_OFFSET 0x0c
/* +0x10: the driver entry-point table whose ADDRESS WIN_$CINIT hands to
 * DISK_$REGISTER (`lea (0x10,A3),A0` at 0x00E303AA). */
#define WIN_JUMP_TABLE_OFFSET 0x10
#define WIN_EC_ARRAY_OFFSET 0x30
#define WIN_CNT_OFFSET 0x40
#define WIN_STATUS_OFFSET 0x58
#define WIN_DEV_INFO_OFFSET 0x5c
#define WIN_REQ_PTR_OFFSET 0x60
#define WIN_EXT_STATUS_OFFSET 0x6c
#define WIN_DISK_STATUS_OFFSET 0x6e
#define WIN_CUR_CYL_OFFSET 0x74
#define WIN_FLAG_OFFSET 0x76

/* Per-unit entry size */
#define WIN_UNIT_ENTRY_SIZE 0x0c

/*
 * Statistics counter structure at WIN_DATA_BASE + 0x40
 * 22 bytes total (5 longs + 1 word)
 */
typedef struct {
  uint32_t seek_errors; /* +0x00 */
  uint32_t not_ready;   /* +0x04: offset 0x48 from base */
  uint32_t reserved1;   /* +0x08 */
  uint32_t equip_check; /* +0x0c: offset 0x4e from base */
  uint32_t reserved2;   /* +0x10 */
  uint16_t data_check;  /* +0x14: offset 0x52 from base */
  uint16_t dma_overrun; /* +0x16: offset 0x54 from base */
} win_stats_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(win_stats_t, seek_errors) == 0x00, "win_stats_t.seek_errors");
_Static_assert(__builtin_offsetof(win_stats_t, not_ready) == 0x04, "win_stats_t.not_ready");
_Static_assert(__builtin_offsetof(win_stats_t, reserved1) == 0x08, "win_stats_t.reserved1");
_Static_assert(__builtin_offsetof(win_stats_t, equip_check) == 0x0C, "win_stats_t.equip_check");
_Static_assert(__builtin_offsetof(win_stats_t, reserved2) == 0x10, "win_stats_t.reserved2");
_Static_assert(__builtin_offsetof(win_stats_t, data_check) == 0x14, "win_stats_t.data_check");
_Static_assert(__builtin_offsetof(win_stats_t, dma_overrun) == 0x16, "win_stats_t.dma_overrun");

/*
 * ANSI command codes for Winchester drives
 */
#define ANSI_CMD_CLEAR_FAULT 0x01
#define ANSI_CMD_REPORT_GENERAL_STATUS 0x0F
#define ANSI_CMD_REPORT_DRIVE_ATTRIBUTE 0x10
#define ANSI_CMD_WRITE_CONTROL 0x41
#define ANSI_CMD_LOAD_ATTRIBUTE_NUMBER 0x50
#define ANSI_CMD_SPIN_CONTROL 0x55

/*
 * WIN_$ANSI_COMMAND (0x00E19128) treats commands >= 0x40 as taking an input
 * byte and commands < 0x40 as returning an output byte, so of the codes above
 * only WRITE_CONTROL, LOAD_ATTRIBUTE_NUMBER and SPIN_CONTROL read the
 * ansi_in_param cell.
 */
#define ANSI_CMD_TAKES_INPUT 0x40

/*
 * The register block each unit record points at (unit + 0x04).  These are the
 * fields DISK_INIT, WIN_$ANSI_COMMAND and WIN_$CHECK_DISK_STATUS touch; the
 * gaps are unexamined.
 */
#define WIN_REG_COMMAND 0x00   /* ANSI command code / extended status */
#define WIN_REG_PARAM 0x02     /* input or output parameter byte */
#define WIN_REG_STATUS 0x06    /* status word, see WIN_STAT_* below */
#define WIN_REG_MODE 0x0C      /* written 0x01 / 0x0A before a command */
#define WIN_REG_GO 0x0E        /* command type: 5 ANSI, 6 init, 3 format, 0 idle */

/* Values WIN_$FORMAT_TRACK writes (0x00E1970C / 0x00E19712). */
#define WIN_MODE_FORMAT 0x09   /* -> WIN_REG_MODE */
#define WIN_GO_FORMAT   0x03   /* -> WIN_REG_GO */

/* Bits of the WIN_REG_STATUS word, from the tests that read it. */
#define WIN_STAT_BUSY 0x8000   /* 0x00E190EC `tst.w` / `bpl`: bit 15 */
#define WIN_STAT_NOT_READY 0x0080 /* 0x00E1910A `tst.b D3b` / `bpl`: bit 7 */

/* Drive identifiers, the low nibble of the LOAD ATTRIBUTE NUMBER result. */
#define WIN_DRIVE_MICROPOLIS_1203 3 /* reported as 0x0103 */
#define WIN_DRIVE_PRIAM_3450 4      /* reported as 0x0104 */
#define WIN_DRIVE_PRIAM_7050 5      /* reported as 0x0105 */
#define WIN_DRIVE_ID_BASE 0x0100    /* 0x00E19B08: addi.w #0x100 */

/*
 * Status codes
 */
#define status_$disk_seek_error 0x00080015
#define status_$disk_driver_logic_error 0x00080022
#define status_$unknown_error_status_from_drive 0x00080023
#define status_$unrecognized_drive_id 0x00080024

/*
 * Global data
 */
extern win_stats_t WIN_$CNT;

/*
 * Function prototypes - Public API
 */

/* Initialization */
status_$t WIN_$CINIT(void *controller);
/* WIN_$DINIT (0x00E19CE8): jump table +0x08, dinit(unit, controller, ...);
 * the CONTROLLER word (argument 2) selects the unit record, and DISK_INIT
 * is called with the two words swapped.  See win/dinit.c. */
uint32_t WIN_$DINIT(uint16_t unit, uint16_t controller, void *num_blocks,
                    void *sec_per_track, void *num_heads, void *pvlabel_info,
                    void *param_7);

/* I/O operations */
/* WIN_$DO_IO is declared below win_$request_t. */

/* Command interface */
status_$t WIN_$ANSI_COMMAND(uint16_t unit, uint16_t ansi_cmd,
                            char *ansi_in_param, char *ansi_out_param);
status_$t WIN_$CHECK_DISK_STATUS(uint16_t unit);

/*
 * The I/O request records WIN_$DO_IO walks as a singly linked list.  Only the
 * fields the WIN driver actually touches are named; everything else is a hole.
 */
typedef struct win_$request_t {
    uint32_t  next;                 /* 0x00: 0 ends the chain (0x00E19956
                                     *       `movea.l (A0),A0` / `cmpa.w #0`).
                                     *       A target VA, not a C pointer: a
                                     *       real pointer would break the
                                     *       layout on a 64-bit host.  Use
                                     *       ARCH_VA_TO_PTR. */
    uint16_t  cylinder;             /* 0x04: SEEK 0x00E19592 `move.w (0x4,A0)`,
                                     *       WIN_$INT 0x00E19C5E */
    uint8_t   head;                 /* 0x06: SEEK 0x00E1959C `move.b (0x6,A0)` */
    uint8_t   sector;               /* 0x07 */
    uint32_t  _unknown_08;          /* 0x08 */
    status_$t status;               /* 0x0C: WIN_$FORMAT_TRACK 0x00E19768
                                     *       `move.l D0,(0xc,A0)`, WIN_$DO_IO
                                     *       0x00E1994E */
    uint32_t  pa;                   /* 0x10: shifted right 10 to make the page
                                     *       number PARITY_$CHK_IO is given */
    uint32_t  length;               /* 0x14 */
    uint8_t   _unknown_18[6];       /* 0x18 */
    uint8_t   proc_id;              /* 0x1E: the requesting process id, the
                                     *       0-based index into the disk
                                     *       subsystem's per-process slot
                                     *       array (disk_$per_proc_t) */
    int8_t    flags;                /* 0x1F: low nibble is the operation
                                     *       (2 = read/write chain, 3 = format,
                                     *       0x00E1979A `moveq #0xf,D0` /
                                     *       `and.b (0x1f,A2),D0b`); the sign
                                     *       bit is tested on a data check */
} win_$request_t;

_Static_assert(__builtin_offsetof(win_$request_t, next) == 0x00, "win_$request_t.next");
_Static_assert(__builtin_offsetof(win_$request_t, cylinder) == 0x04, "win_$request_t.cylinder");
_Static_assert(__builtin_offsetof(win_$request_t, head) == 0x06, "win_$request_t.head");
_Static_assert(__builtin_offsetof(win_$request_t, status) == 0x0C, "win_$request_t.status");
_Static_assert(__builtin_offsetof(win_$request_t, pa) == 0x10, "win_$request_t.pa");
_Static_assert(__builtin_offsetof(win_$request_t, length) == 0x14, "win_$request_t.length");
_Static_assert(__builtin_offsetof(win_$request_t, proc_id) == 0x1E, "win_$request_t.proc_id");
_Static_assert(__builtin_offsetof(win_$request_t, flags) == 0x1F, "win_$request_t.flags");

/*
 * WIN_$FORMAT_TRACK (0x00E196AA) - the op-type 3 arm of WIN_$DO_IO.
 * Takes TWO arguments: 0x00E197B2 pushes `pea (A2)` (the request) and then
 * `move.l (0x8,A6),-(SP)` (the device entry), and the callee reads both
 * (0x00E196B2 `move.l (0x8,A6),D3` / 0x00E196B6 `move.l (0xc,A6),D4`).
 */
void WIN_$FORMAT_TRACK(void *dev_entry, win_$request_t *req);

/* WIN_$DO_IO (0x00E19776): jump table +0x10, do_io(vol, req, param_3,
 * result); `dev_entry` is the disk_$volume_t, whose dev_unit (+0x1C) is
 * what SEEK and win_$reinit_drive are given. */
void WIN_$DO_IO(void *dev_entry, win_$request_t *req, void *param_3,
                int8_t *result);

/* Control operations */
uint32_t WIN_$SPIN_DOWN(uint16_t *unit_ptr);
/* WIN_$INT (0x00E19BFA): the DCTE's cnum is the unit; returns Domain true. */
int8_t WIN_$INT(dcte_t *dcte);

/* Queue and stats */
/* WIN_$ERROR_QUE (0x00E19D54): jump table +0x14, error_que(vol, is_timeout,
 * result); clears *result and never loads D0. */
void WIN_$ERROR_QUE(void *vol, uint16_t is_timeout, int8_t *result);
/* WIN_$GET_STATS (0x00E19D62): jump table +0x18, get_stats(cnum, unit, stats);
 * 22 bytes of WIN_$CNT for (0, 0), 22 zero bytes otherwise. */
void WIN_$GET_STATS(int16_t cnum, int16_t unit, void *stats);

/*
 * External functions used by WIN: EC_$* come from ec/ec.h, DISK_$REGISTER
 * and DISK_$SORT from disk/disk.h, PARITY_$CHK_IO from parity/parity.h,
 * CRASH_SYSTEM and the Disk_*_err messages from misc/crash_system.h.
 *
 * DISK_INIT (0x00e19986) is WIN-internal, not a disk-subsystem entry point:
 * it sits inside the WIN code region and its only callers are WIN_$DINIT
 * (0x00e19d32) and FUN_00e194b4 (0x00e194e0), so this prototype belongs here
 * and not in disk/disk.h.  See win/disk_init.c.
 *
 * It is reached with `bsr` and returns its status in D0 with no Pascal result
 * slot, so it is a module-internal routine rather than an exported entry.
 * Its arguments are all by reference except the two leading words:
 *
 *   unit             (0x08,A6) word, the drive
 *   sub_unit         (0x0A,A6) word, must be 0
 *   total_blocks     (0x0C,A6) long*  in/out: >0 on entry means "geometry
 *                              already known"; otherwise filled in
 *   blocks_per_track (0x10,A6) word*  out
 *   heads            (0x14,A6) word*  out
 *   geometry         (0x18,A6) word[2]* out: [0] always 0, [1] the cylinder
 *                              count reported to the caller
 *   drive_id         (0x1C,A6) word*  out: 0x0100 | (attribute & 0x0F)
 */
status_$t DISK_INIT(uint16_t unit, uint16_t sub_unit, int32_t *total_blocks,
                    uint16_t *blocks_per_track, uint16_t *heads,
                    uint16_t *geometry, uint16_t *drive_id);

#endif /* WIN_H */
