/*
 * FLP - Floppy Disk Driver
 *
 * This module provides floppy disk support for Domain/OS.
 * It implements controller initialization, device initialization,
 * I/O operations, and interrupt handling.
 *
 * The floppy controller uses memory-mapped I/O and generates
 * interrupts for completion notification.
 */

#ifndef FLP_H
#define FLP_H

#include "base/base.h"
#include "ec/ec.h"
#include "ml/ml.h"
#include "parity/parity.h"
#include "wp/wp.h"
#include "dma/dma.h"   /* DMA_$CHECK */

/*
 * Maximum number of floppy units supported
 */
#define FLP_MAX_UNITS 4

/*
 * Floppy status codes
 */
#define status_$io_controller_not_in_system 0x00100002
#define status_$disk_controller_error 0x00080004
#define status_$invalid_unit_number 0x00080018

/*
 * Floppy controller registers structure
 * Accessed via memory-mapped I/O at DAT_00e7b020
 */
typedef struct {
  uint8_t _reserved[0x10];
  uint8_t status; /* 0x10: Status register */
  uint8_t _pad1;
  uint8_t data; /* 0x12: Data register */
  uint8_t _pad2;
  uint8_t control; /* 0x14: Control register */
} flp_regs_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(flp_regs_t, status) == 0x10, "flp_regs_t.status");
_Static_assert(__builtin_offsetof(flp_regs_t, data) == 0x12, "flp_regs_t.data");
_Static_assert(__builtin_offsetof(flp_regs_t, control) == 0x14, "flp_regs_t.control");

/*
 * Status register bits
 */
#define FLP_STATUS_BUSY 0x80     /* Controller busy */
#define FLP_STATUS_DIO 0x40      /* Data I/O direction */
#define FLP_STATUS_CMD_MASK 0x1F /* Command status mask */

/*
 * ============================================================================
 * FLP_DATA - the floppy module's data block at 0x00E7AEF4
 * ============================================================================
 *
 * The SAU2 map has `D E7AEF4 FLP_ size = 13C`, running 0x00E7AEF4..0x00E7B030
 * (the OS_CAL_WIRED segment starts there).  Every FLP_ routine establishes it
 * with `lea (0xe7aef4).l,A5`, so all the cells the driver touches are fields
 * of this one block rather than separate objects; the names below that start
 * with DAT_ are the Ghidra labels for fields whose purpose is only partly
 * recovered (bead source-wk2f).
 *
 * The two interior symbols the map names are FLP_$EC (+0x60) and FLP_$SREGS
 * (+0x70).  The image contents come from `gsk read 0x00E7AEF4 0x13C`; the
 * command blocks in it are recognisable NEC 8272 FDC command strings, which
 * is what pins their extents:
 *
 *   +0x02C  4D 00 03 08 74 4E              FORMAT TRACK, 6 words
 *                                          (the count cell at 0x00E3DDC4)
 *   +0x04A  00 00 00 00 00 03 08 35 FF     READ/WRITE DATA, 9 words
 *                                          (the count cell at 0x00E3DFE0)
 *   +0x108  03 DF 3C                       SPECIFY, 3 words
 *                                          (the count cell at 0x00E3DDC2)
 *   +0x110  04 00                          SENSE DRIVE STATUS, 2 words
 *   +0x114  07 00                          RECALIBRATE, 2 words
 *                                          (the count cell at 0x00E3E21C)
 *   +0x118  0F 00 00                       SEEK, 3 words
 */
typedef struct flp_data_t {
  /* +0x000 FLP_$JUMP_TABLE: the driver entry points DISK_$REGISTER is given */
  m68k_ptr_t jump_table[7];
  /* +0x01C FLP_$SREGS_ARRAY */
  uint16_t sregs_array[8];
  /* +0x02C FORMAT TRACK command block: cmd, unit/head, N, SC, GPL, D */
  uint16_t fmt_cmd[6];
  /* +0x038 result-register array FLP_$INT fills from the FDC
   * (flp/int.c: `(uint8_t *)&FLP_$JUMP_TABLE + 0x38`) */
  uint16_t result_regs[9];
  /* +0x04A READ/WRITE DATA command block: cmd, unit/head, cyl, head, sector,
   * N, EOT, GPL, DTL.  The first three words are also the SEEK command. */
  uint16_t rw_cmd[9];
  uint16_t w_05c[2];              /* +0x05C */
  ec_$eventcount_t ec;            /* +0x060 FLP_$EC */
  uint16_t w_06c[2];              /* +0x06C */
  uint16_t sregs[4];              /* +0x070 FLP_$SREGS (ST0..ST3 result words) */
  uint8_t unit_cyl[8];            /* +0x078 current cylinder, 2 bytes per unit */
  uint8_t io_buffer[0x68];        /* +0x080 FLP_IO_BUFFER */
  uint8_t ctlr_table[0x14];       /* +0x0E8 8 bytes per controller: info ptr
                                   * at +0, hardware address at +4 */
  uint32_t fmt_buf_pa;            /* +0x0FC physical address of io_buffer */
  uint16_t w_100;                 /* +0x100 */
  uint16_t fmt_n;                 /* +0x102 sector-size code N; the driver
                                   * writes only its low byte into the format
                                   * buffer */
  uint16_t w_104;                 /* +0x104 */
  uint16_t base_cmd;              /* +0x106 MFM base command byte (0x40) */
  uint16_t specify_cmd[4];        /* +0x108 SPECIFY command block */
  uint16_t sense_cmd[2];          /* +0x110 SENSE DRIVE STATUS: cmd, unit/head */
  uint16_t recal_cmd[2];          /* +0x114 RECALIBRATE: cmd, unit */
  uint16_t seek_cmd[4];           /* +0x118 SEEK: cmd, unit/head, cylinder */
  uint8_t unit_active[FLP_MAX_UNITS];  /* +0x120 */
  uint8_t disk_change[FLP_MAX_UNITS];  /* +0x124 */
  uint32_t buf_pa;                /* +0x128 physical address of the I/O buffer */
  int32_t hw_addr;                /* +0x12C controller register base */
  int16_t dma_retry;              /* +0x130 */
  uint16_t cmd_retry;             /* +0x132 */
  uint16_t w_134;                 /* +0x134 */
  uint16_t unit_count;            /* +0x136 DISK_$REGISTER's unit-count word */
  int8_t initialized;             /* +0x138 -1 once FLP_$DINIT has run */
  uint8_t pad_139[3];             /* +0x139 */
} flp_data_t;

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(flp_data_t, sregs_array) == 0x01C, "flp_data_t.sregs_array");
_Static_assert(__builtin_offsetof(flp_data_t, fmt_cmd) == 0x02C, "flp_data_t.fmt_cmd");
_Static_assert(__builtin_offsetof(flp_data_t, result_regs) == 0x038, "flp_data_t.result_regs");
_Static_assert(__builtin_offsetof(flp_data_t, rw_cmd) == 0x04A, "flp_data_t.rw_cmd");
_Static_assert(__builtin_offsetof(flp_data_t, ec) == 0x060, "flp_data_t.ec");
_Static_assert(__builtin_offsetof(flp_data_t, sregs) == 0x070, "flp_data_t.sregs");
_Static_assert(__builtin_offsetof(flp_data_t, unit_cyl) == 0x078, "flp_data_t.unit_cyl");
_Static_assert(__builtin_offsetof(flp_data_t, io_buffer) == 0x080, "flp_data_t.io_buffer");
_Static_assert(__builtin_offsetof(flp_data_t, ctlr_table) == 0x0E8, "flp_data_t.ctlr_table");
_Static_assert(__builtin_offsetof(flp_data_t, fmt_buf_pa) == 0x0FC, "flp_data_t.fmt_buf_pa");
_Static_assert(__builtin_offsetof(flp_data_t, fmt_n) == 0x102, "flp_data_t.fmt_n");
_Static_assert(__builtin_offsetof(flp_data_t, base_cmd) == 0x106, "flp_data_t.base_cmd");
_Static_assert(__builtin_offsetof(flp_data_t, specify_cmd) == 0x108, "flp_data_t.specify_cmd");
_Static_assert(__builtin_offsetof(flp_data_t, sense_cmd) == 0x110, "flp_data_t.sense_cmd");
_Static_assert(__builtin_offsetof(flp_data_t, recal_cmd) == 0x114, "flp_data_t.recal_cmd");
_Static_assert(__builtin_offsetof(flp_data_t, seek_cmd) == 0x118, "flp_data_t.seek_cmd");
_Static_assert(__builtin_offsetof(flp_data_t, unit_active) == 0x120, "flp_data_t.unit_active");
_Static_assert(__builtin_offsetof(flp_data_t, disk_change) == 0x124, "flp_data_t.disk_change");
_Static_assert(__builtin_offsetof(flp_data_t, buf_pa) == 0x128, "flp_data_t.buf_pa");
_Static_assert(__builtin_offsetof(flp_data_t, hw_addr) == 0x12C, "flp_data_t.hw_addr");
_Static_assert(__builtin_offsetof(flp_data_t, dma_retry) == 0x130, "flp_data_t.dma_retry");
_Static_assert(__builtin_offsetof(flp_data_t, cmd_retry) == 0x132, "flp_data_t.cmd_retry");
_Static_assert(__builtin_offsetof(flp_data_t, unit_count) == 0x136, "flp_data_t.unit_count");
_Static_assert(__builtin_offsetof(flp_data_t, initialized) == 0x138, "flp_data_t.initialized");
_Static_assert(sizeof(flp_data_t) == 0x13C,
               "flp_data_t: map segment FLP_ 0x00E7AEF4 size = 13C");
#endif

extern flp_data_t FLP_DATA;

/* Event counter for floppy operations (FLP_DATA + 0x60 = 0xe7af54) */
#define FLP_$EC (FLP_DATA.ec)

/* FDC result status registers (FLP_DATA + 0x70 = 0xe7af64) */
#define FLP_$SREGS (FLP_DATA.sregs[0])

/* Jump table for floppy operations (FLP_DATA + 0x00 = 0xe7aef4) */
#define FLP_$JUMP_TABLE (FLP_DATA.jump_table[0])

/* Current controller address (FLP_DATA + 0x12c = 0xe7b020) */
#define DAT_00e7b020 (FLP_DATA.hw_addr)

/*
 * Function prototypes
 */

/*
 * FLP_$CINIT - Controller initialization
 *
 * Initializes a floppy disk controller and registers it with
 * the disk subsystem.
 *
 * @param ctlr_info  Controller information structure
 * @return Status code
 */
status_$t FLP_$CINIT(void *ctlr_info);

/*
 * FLP_$DINIT - Device initialization
 *
 * Initializes a specific floppy drive unit.
 *
 * @param unit      Unit number (0-3)
 * @param ctlr      Controller number
 * @param params    I/O: Disk parameters (cylinders, etc.)
 * @param heads     Output: Number of heads
 * @param sectors   Output: Sectors per track
 * @param geometry  Output: Geometry info
 * @param flags     Output: Drive flags
 * @return Status code
 */
status_$t FLP_$DINIT(uint16_t unit, uint16_t ctlr, int32_t *params,
                     uint16_t *heads, uint16_t *sectors, uint32_t *geometry,
                     uint16_t *flags);

/*
 * FLP_$SHUTDOWN - Shutdown a floppy unit
 *
 * Marks a floppy unit as inactive and returns count of remaining
 * active units.
 *
 * @param unit  Unit number to shut down
 * @return Number of remaining active units
 */
int16_t FLP_$SHUTDOWN(uint16_t unit);

/*
 * FLP_$INT - Interrupt handler
 *
 * Handles floppy disk controller interrupts, reading status
 * and result bytes from the controller.
 *
 * @param int_info  Interrupt information structure
 * @return 0xFF (interrupt handled)
 */
uint16_t FLP_$INT(void *int_info);

/*
 * FLP_$REVALIDATE - Revalidate disk
 *
 * Clears the disk change flag for a unit, allowing operations
 * to proceed after a disk change.
 *
 * @param disk_info  Disk information structure
 */
void FLP_$REVALIDATE(void *disk_info);

/*
 * FLP_$DO_IO - Perform I/O operation
 *
 * Wrapper that calls the internal FLP_DO_IO function with
 * properly formatted parameters.
 *
 * @param param_1  I/O request block
 * @param param_2  Buffer
 * @param param_3  Count
 * @param param_4  LBA (packed)
 */
void FLP_$DO_IO(void *param_1, void *param_2, void *param_3, uint32_t param_4);

/* Internal functions */
void FLP_DO_IO(void *req, void *buf, void *param3, uint16_t lba_hi,
               uint32_t lba_lo);
status_$t SHAKE(uint16_t *data_buf, int16_t *count_ptr, int16_t *dir_ptr);
/*
 * EXCS hands its second argument straight to SHAKE as the byte count
 * ("move.l (0xc,A6),-(SP)" at 0x00E3E28C), so it is the address of a word.
 */
status_$t EXCS(uint16_t *cmd_buf, int16_t *count_ptr, void *req);
void FLP_FORMAT_TRACK(void *req, void *buf);

/* External functions used by FLP */
/* WP_$WIRE declared in wp/wp.h */
/* ML_$LOCK, ML_$UNLOCK declared in ml/ml.h */
/* PARITY_$CHK_IO declared in parity/parity.h */
/* DMA_$CHECK is declared in dma/dma.h (bead source-3uo). */

#endif /* FLP_H */
