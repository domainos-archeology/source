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

/*
 * ============================================================================
 * Internal Data Declarations - Controller/Data Area (0xe7af00 range)
 * ============================================================================
 */

/* Controller info table - 8 bytes per controller */
extern uint8_t DAT_00e7afdc[];  /* +0xe8: Controller info pointer table */
extern uint8_t DAT_00e7afe0[];  /* +0xec: Controller address table */

/* Physical address/buffer info */
extern uint32_t DAT_00e7aff0;   /* +0xfc: Physical address of format buffer */
extern uint8_t DAT_00e7affc[];  /* +0x108: Specify command data */
extern uint8_t DAT_00e7aff7;    /* N value for format */

/* Status registers */
extern uint16_t DAT_00e7af66;   /* +0x72: Status registers 1-2 (ST1/ST2) */
extern uint8_t DAT_00e7af69;    /* +0x75: Status register 2 low byte */
extern uint8_t DAT_00e7af6c[];  /* +0x78: Unit status array (2 bytes/unit) */
extern uint16_t FLP_$SREGS_ARRAY[]; /* Array at FLP_$JUMP_TABLE + 0x1c */

/* Command buffers */
extern uint16_t DAT_00e7af20;   /* Command byte (format) */
extern uint16_t DAT_00e7af22;   /* Unit + head (format) */
extern uint16_t DAT_00e7af3e;   /* +0x4a: Command byte */
extern uint16_t DAT_00e7af40;   /* +0x4c: Head + unit */
extern uint16_t DAT_00e7af42;   /* +0x4e: Cylinder */
extern uint16_t DAT_00e7af44;   /* +0x50: Head number */
extern uint16_t DAT_00e7af46;   /* +0x52: Sector number */
extern uint16_t DAT_00e7affa;   /* +0x106: Base command code */

/* I/O buffer area at 0xe7af74 */
extern uint8_t FLP_IO_BUFFER[];

/*
 * ============================================================================
 * Internal Data Declarations - Command Area (0xe7b000 range)
 * ============================================================================
 */

/* Command bytes */
extern uint8_t DAT_00e7b004;    /* +0x110: Command byte 0 (sense drive status) */
extern uint16_t DAT_00e7b006;   /* +0x112: Command byte 1 (unit + head) */
extern uint8_t DAT_00e7b008[];  /* +0x114: Recalibrate/EXCS data area */
extern uint16_t DAT_00e7b00a;   /* +0x116: Current unit number */
extern uint16_t DAT_00e7b00c;   /* Seek command */
extern uint16_t DAT_00e7b00e;   /* Unit + head (seek) */
extern uint16_t DAT_00e7b010;   /* Cylinder (seek) */

/* Unit flags */
extern uint8_t DAT_00e7b014[];  /* +0x120: Unit active flags */
extern uint8_t DAT_00e7b018[];  /* +0x124: Disk change flags */

/* Physical address and retry control */
extern uint32_t DAT_00e7b01c;   /* +0x128: Physical buffer address */
extern int16_t DAT_00e7b024;    /* +0x130: Retry/DMA retry count */
extern uint16_t DAT_00e7b026;   /* +0x132: Retry/control flag */
extern uint8_t DAT_00e7b02a[];  /* Registration data */
extern int8_t DAT_00e7b02c;     /* +0x138: Initialized flag */

/*
 * ============================================================================
 * Internal Data Declarations - ROM Constants (0xe3xxxx range)
 * ============================================================================
 */

/* Hardware signature and command parameters */
extern uint8_t DAT_00e3ddc2[];  /* Seek command parameters */
extern uint8_t DAT_00e3ddc4[];  /* Format command parameters */
extern uint8_t DAT_00e3dfe0[];  /* Read/write command parameters */

/* Direction/count values (used as pointers) */
extern int16_t DAT_00e3e10e;    /* Read direction (0) */
extern int16_t DAT_00e3e110;    /* Write direction (1) / count 1 */
extern int16_t DAT_00e3e21c;    /* Count 2 */

/* Disk geometry constants */
extern uint32_t DAT_00e3e21e;   /* Geometry word 1 */
extern uint32_t DAT_00e3e222;   /* Geometry word 2 */
extern uint16_t DAT_00e3e226;   /* Geometry word 3 */

/* Disk error counter */
extern uint8_t DAT_00e7a55c[];

/*
 * ============================================================================
 * Event Counter Pointer (for EC_$WAIT)
 * ============================================================================
 */

extern uint32_t DAT_00e2b0d4;   /* Secondary event counter */

/*
 * ============================================================================
 * Internal Functions
 * ============================================================================
 */

/* Hardware probe function */
int8_t FUN_00e29138(void *signature, void *hw_addr, void *buffer);

#endif /* FLP_INTERNAL_H */
