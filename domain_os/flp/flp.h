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

#include "../base/base.h"

/*
 * Maximum number of floppy units supported
 */
#define FLP_MAX_UNITS   4

/*
 * Floppy status codes
 */
#define status_$io_controller_not_in_system   0x00100002
#define status_$disk_controller_error         0x00080004
#define status_$invalid_unit_number           0x00080018

/*
 * Floppy controller registers structure
 * Accessed via memory-mapped I/O at DAT_00e7b020
 */
typedef struct {
    uint8_t _reserved[0x10];
    uint8_t status;         /* 0x10: Status register */
    uint8_t _pad1;
    uint8_t data;           /* 0x12: Data register */
    uint8_t _pad2;
    uint8_t control;        /* 0x14: Control register */
} flp_regs_t;

/*
 * Status register bits
 */
#define FLP_STATUS_BUSY     0x80    /* Controller busy */
#define FLP_STATUS_DIO      0x40    /* Data I/O direction */
#define FLP_STATUS_CMD_MASK 0x1F    /* Command status mask */

/*
 * Global data area at 0xe7aef4 (FLP_$DATA)
 * Layout based on analysis of code:
 *   +0x60:  FLP__EC (event counter)
 *   +0x70:  FLP__SREGS (status registers array)
 *   +0x78:  Unit status array (2 bytes per unit)
 *   +0x80:  I/O buffer area
 *   +0xe8:  Controller table (8 bytes per controller)
 *   +0xfc:  DAT_00e7aff0 (physical address)
 *   +0x108: DAT_00e7affc
 *   +0x114: DAT_00e7b008
 *   +0x116: DAT_00e7b00a (unit number)
 *   +0x120: DAT_00e7b014 (unit active flags)
 *   +0x124: DAT_00e7b018 (disk change flags)
 *   +0x12c: DAT_00e7b020 (controller address)
 *   +0x132: DAT_00e7b026
 *   +0x136: DAT_00e7b02a
 *   +0x138: DAT_00e7b02c (initialized flag)
 */

/* Event counter for floppy operations */
extern void *FLP__EC;

/* Saved registers from interrupt */
extern uint16_t FLP__SREGS;

/* Jump table for floppy operations */
extern void *FLP__JUMP_TABLE;

/* Current controller address */
extern int32_t DAT_00e7b020;

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
status_$t FLP_$DINIT(uint16_t unit, uint16_t ctlr,
                      int32_t *params, uint16_t *heads,
                      uint16_t *sectors, uint32_t *geometry,
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
extern void FLP_DO_IO(void *p1, void *p2, void *p3, uint16_t p4_hi, uint32_t p4_lo);
extern status_$t SHAKE(void *regs, void *data1, void *data2);
extern void DISK__REGISTER(void *p1, void *p2, void *p3, void *p4, void **p5);
extern void EC__INIT(void *ec);
extern void EC__ADVANCE_WITHOUT_DISPATCH(void *ec);
extern status_$t EXCS(void *p1, void *p2, void *p3);
extern uint32_t MMU__VTOP(uint32_t va, status_$t *status);
extern void WP__WIRE(uint32_t phys);

#endif /* FLP_H */
