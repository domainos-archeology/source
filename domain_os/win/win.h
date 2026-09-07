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

/*
 * WIN data area base at 0xe2b89c.
 *
 * Every WIN entry point loads it with `lea (0xe2b89c).l,A5` (e.g. DISK_INIT
 * at 0x00E1998E, WIN_$DINIT at 0x00E19CF0) and then addresses the module
 * globals as (off,A5).  On a host build the same offsets are applied to a
 * plain array the unit tests supply, so the translated code can be driven
 * without an Apollo.
 */
#if defined(ARCH_M68K)
#define WIN_DATA_BASE ((uint8_t *)0x00e2b89c)
#else
#define WIN_DATA_SIZE 0x78
extern uint8_t WIN_$DATA[WIN_DATA_SIZE];
#define WIN_DATA_BASE (WIN_$DATA)
#endif

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
#define WIN_REG_GO 0x0E        /* command type: 5 ANSI, 6 init, 0 idle */

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
#define status_$io_controller_not_in_system 0x00100002
#define status_$disk_not_ready 0x00080001
#define status_$disk_controller_busy 0x00080002
#define status_$disk_controller_timeout 0x00080003
#define status_$disk_equipment_check 0x00080005
#define status_$disk_data_check 0x00080009
#define status_$DMA_overrun 0x0008000a
#define status_$disk_seek_error 0x00080015
#define status_$unknown_error_status_from_drive 0x00080023
#define status_$unrecognized_drive_id 0x00080024
#define status_$memory_parity_error_during_disk_write 0x00080025

/*
 * Global data
 */
extern win_stats_t WIN_$CNT;

/*
 * Function prototypes - Public API
 */

/* Initialization */
status_$t WIN_$CINIT(void *controller);
uint32_t WIN_$DINIT(uint16_t vol_idx, uint16_t unit, void *param_3,
                    void *param_4, void *param_5, void *param_6, void *param_7);

/* I/O operations */
void WIN_$DO_IO(void *dev_entry, int32_t *req, void *param_3, uint8_t *result);

/* Command interface */
status_$t WIN_$ANSI_COMMAND(uint16_t unit, uint16_t ansi_cmd,
                            char *ansi_in_param, char *ansi_out_param);
status_$t WIN_$CHECK_DISK_STATUS(uint16_t unit);

/* Control operations */
uint32_t WIN_$SPIN_DOWN(uint16_t *unit_ptr);
uint32_t WIN_$INT(void *param);

/* Queue and stats */
void WIN_$ERROR_QUE(uint8_t param_1, uint8_t *param_2);
void WIN_$GET_STATS(int16_t param_1, int16_t param_2, void *stats);

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
