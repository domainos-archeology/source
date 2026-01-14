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

/*
 * WIN data area base at 0xe2b89c
 */
#define WIN_DATA_BASE ((uint8_t *)0x00e2b89c)

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
 *   +0x40: Statistics counters (WIN__CNT)
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
#define ANSI_CMD_SPIN_CONTROL 0x55

/*
 * Status codes
 */
#define status_$io_controller_not_in_system 0x00100002
#define status_$disk_not_ready 0x00080001
#define status_$disk_controller_timeout 0x00080003
#define status_$disk_equipment_check 0x00080005
#define status_$disk_data_check 0x00080009
#define status_$DMA_overrun 0x0008000a
#define status_$disk_seek_error 0x00080015
#define status_$unknown_error_status_from_drive 0x00080023
#define status_$memory_parity_error_during_disk_write 0x00080025

/*
 * Global data
 */
extern void *WIN__JUMP_TABLE;
extern win_stats_t WIN__CNT;

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
 * Internal functions
 */
extern status_$t SEEK(uint16_t unit, uint16_t cylinder, void *req,
                      uint8_t flags);
extern status_$t read_or_write_disk_record(uint16_t unit);
extern status_$t check_dma_error(uint16_t param);
extern status_$t FUN_00e190bc(uint16_t unit);
extern status_$t FUN_00e194b4(uint16_t param_1, uint16_t cylinder);
extern void FUN_00e196aa(void *dev_entry);
extern void FUN_00e19186(uint16_t unit, char status, uint16_t *out);

/*
 * External functions used by WIN
 */
/* ML_$LOCK, ML_$UNLOCK declared in ml/ml.h */
extern void EC__INIT(void *ec);
extern int16_t EC__WAIT(void *ec_array, void *wait_val);
extern void EC__ADVANCE_WITHOUT_DISPATCH(void *ec);
extern void DISK__REGISTER(void *type, void *ctrl, void *flags, void *dev_type,
                           void **jump_table);
extern void DISK__SORT(void *dev_entry, void **queue_ptr);
extern uint32_t DISK_INIT(uint16_t unit, uint16_t vol_idx, void *p3, void *p4,
                          void *p5, void *p6, void *p7);
extern int16_t PARITY__CHK_IO(uint32_t addr, uint32_t size);
extern void CRASH_SYSTEM(void *error);

/* Error messages */
extern void *Disk_controller_err;
extern void *Disk_driver_logic_err;

#endif /* WIN_H */
