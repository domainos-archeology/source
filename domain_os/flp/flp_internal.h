/*
 * flp/flp_internal.h - Internal Floppy Driver Definitions
 *
 * Contains internal functions, data, and types used only within
 * the floppy subsystem. External consumers should use flp/flp.h.
 */

#ifndef FLP_INTERNAL_H
#define FLP_INTERNAL_H

#include "flp/flp.h"
#include "ec/ec.h"
#include "disk/disk.h"
#include "io/io.h"   /* io_$probe */

/*
 * ============================================================================
 * Fields of the FLP_DATA block (flp/flp.h) the driver reaches by their Ghidra
 * label names.  Each is an alias, not a separate object: the offset in the
 * comment is the cell's displacement from the block base 0x00E7AEF4, which is
 * the A5 value every FLP_ routine loads (bead source-wk2f).
 * ============================================================================
 */

/* +0x01C: the status-register array at FLP_$JUMP_TABLE + 0x1c */
#define FLP_$SREGS_ARRAY  (FLP_DATA.sregs_array)

/* FORMAT TRACK command block, +0x02C..+0x038 */
#define DAT_00e7af20      (FLP_DATA.fmt_cmd[0])   /* +0x02C command byte, 0x4D */
#define DAT_00e7af22      (FLP_DATA.fmt_cmd[1])   /* +0x02E unit + head * 4  */

/* READ/WRITE DATA command block, +0x04A..+0x05C (the first three words are
 * also the SEEK command EXCS sends with the 3-word count at 0x00E3DDC2) */
#define DAT_00e7af3e      (FLP_DATA.rw_cmd[0])    /* +0x04A command byte      */
#define DAT_00e7af40      (FLP_DATA.rw_cmd[1])    /* +0x04C unit + head * 4   */
#define DAT_00e7af42      (FLP_DATA.rw_cmd[2])    /* +0x04E cylinder          */
#define DAT_00e7af44      (FLP_DATA.rw_cmd[3])    /* +0x050 head number       */
#define DAT_00e7af46      (FLP_DATA.rw_cmd[4])    /* +0x052 sector number     */

/* FDC result status registers, +0x070..+0x078 (FLP_$SREGS is sregs[0]) */
#define DAT_00e7af66      (FLP_DATA.sregs[1])     /* +0x072 ST1/ST2 word      */
/* +0x075: the low byte of sregs[2].  Read-only in the driver, so the low byte
 * is taken with a mask rather than a byte pointer, which keeps the expression
 * correct on a little-endian host. */
#define DAT_00e7af69      ((uint8_t)(FLP_DATA.sregs[2] & 0xFF))

/* +0x078: current cylinder per unit, 2 bytes each */
#define DAT_00e7af6c      (FLP_DATA.unit_cyl)

/* +0x080: the driver's own DMA buffer */
#define FLP_IO_BUFFER     (FLP_DATA.io_buffer)

/* +0x0E8: controller table, 8 bytes per controller.  DAT_00e7afdc names the
 * info-pointer slot and DAT_00e7afe0 the hardware-address slot; both index the
 * same array, which is why the callers add ctlr * 8 to either one. */
#define DAT_00e7afdc      (&FLP_DATA.ctlr_table[0])
#define DAT_00e7afe0      (&FLP_DATA.ctlr_table[4])

#define DAT_00e7aff0      (FLP_DATA.fmt_buf_pa)   /* +0x0FC format buffer PA  */
/* +0x103: the low byte of the sector-size word at +0x102.  Read-only. */
#define DAT_00e7aff7      ((uint8_t)(FLP_DATA.fmt_n & 0xFF))
#define DAT_00e7affa      (FLP_DATA.base_cmd)     /* +0x106 MFM base command  */
#define DAT_00e7affc      (FLP_DATA.specify_cmd)  /* +0x108 SPECIFY block     */

#define DAT_00e7b004      (FLP_DATA.sense_cmd[0]) /* +0x110 SENSE DRIVE STATUS */
#define DAT_00e7b006      (FLP_DATA.sense_cmd[1]) /* +0x112 unit + head       */
#define DAT_00e7b008      (FLP_DATA.recal_cmd)    /* +0x114 RECALIBRATE block */
#define DAT_00e7b00a      (FLP_DATA.recal_cmd[1]) /* +0x116 current unit      */
#define DAT_00e7b00c      (FLP_DATA.seek_cmd[0])  /* +0x118 SEEK command      */
#define DAT_00e7b00e      (FLP_DATA.seek_cmd[1])  /* +0x11A unit + head       */
#define DAT_00e7b010      (FLP_DATA.seek_cmd[2])  /* +0x11C cylinder          */

#define DAT_00e7b014      (FLP_DATA.unit_active)  /* +0x120 per-unit flags    */
#define DAT_00e7b018      (FLP_DATA.disk_change)  /* +0x124 per-unit flags    */
#define DAT_00e7b01c      (FLP_DATA.buf_pa)       /* +0x128 I/O buffer PA     */
#define DAT_00e7b024      (FLP_DATA.dma_retry)    /* +0x130 DMA retry count   */
#define DAT_00e7b026      (FLP_DATA.cmd_retry)    /* +0x132 command retry     */
#define DAT_00e7b02a      (FLP_DATA.unit_count)   /* +0x136 DISK_$REGISTER    */
#define DAT_00e7b02c      (FLP_DATA.initialized)  /* +0x138 init flag         */

/*
 * ============================================================================
 * Literal cells in the FLP_ code region (map segment "I E3DC54 FLP_ size =
 * 8CC").  Domain Pascal passes VAR and const parameters by address, so each
 * literal argument becomes a cell in the code region whose address is pushed.
 * ============================================================================
 */

/* 0xE3DDC2: the word 3 - SHAKE/EXCS byte count (pea (-0x2bc,PC) @0xE3E07C) */
extern int16_t DAT_00e3ddc2;
/* 0xE3DDC4: the word 6 - format-track byte count (pea (0x3e,PC) @0xE3DD84) */
extern int16_t DAT_00e3ddc4;
/* 0xE3DFE0: the word 9 - read/write byte count (pea (0x8c,PC) @0xE3DF52) */
extern int16_t DAT_00e3dfe0;

/* 0xE3E10E: the word 0 - SHAKE's "read" direction, io_$probe's width */
extern int16_t DAT_00e3e10e;
/* 0xE3E110: the word 1 - SHAKE's "write" direction, and the count 1 */
extern int16_t DAT_00e3e110;
/* 0xE3E21C: the word 2 - the recalibrate/sense command word count */
extern int16_t DAT_00e3e21c;

/* 0xE3E21E, 0xE3E222, 0xE3E226: the drive geometry FLP_$DINIT copies out */
extern uint32_t DAT_00e3e21e;
extern uint32_t DAT_00e3e222;
extern uint16_t DAT_00e3e226;

/*
 * ============================================================================
 * The floppy error counter inside the DISK_ module block
 * ============================================================================
 *
 * 0x00E7A55C is DISK_$DATA + 0x390 (disk/disk.h: `D E7A1CC DISK_ size = B90`).
 * The driver clears one counter per request with a 0x1C stride, so the counters
 * run on into the DISK_BPTBL region the map marks at 0x00E7A560 - the cell is
 * an alias into DISK_$DATA, not an object of its own.
 */
#define DAT_00e7a55c      (&DISK_$DATA[0x390])

/*
 * ============================================================================
 * Internal Functions
 * ============================================================================
 */

/* Hardware probe function */
/* io_$probe is declared in io/io.h (bead source-3uo). */

#endif /* FLP_INTERNAL_H */
