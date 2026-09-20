/*
 * DISK_INTERRUPT - disk interrupt dispatcher
 *
 * Original address: 0x00e0aabc
 * Size: 76 bytes
 *
 * Installed in the m68k vector table (referenced as data from 0x00e0ab36) for
 * IO_VECTOR_DISK.  It reads the type-1 controller's status register and hands
 * the interrupt to the type-1 or the type-0 driver depending on which bits are
 * set; an interrupt claimed by neither crashes the system.
 *
 * A5 = 0xe22904 = &IO_$INT_CTRL (saved and restored around the body with
 * pea (A5) / movea.l (-0xc,A6),A5, since the vector entry does not save it).
 *
 * Re-verified against the disassembly on 2026-09-19 (0x00E0AABC -
 * 0x00E0AB06): faithful.  The SAU2 map places it in the `ATBUS_` code
 * segment (E0AABC, size 118) and A5 in the `D E22904 ATBUS_ size = 2C`
 * data block, which the tree names io_int_ctrl_t / IO_$INT_CTRL; the file
 * keeps its disk/ home as listed.
 */

#include "disk/disk_internal.h"
#include "io/io.h"

/*
 * Bits of the controller status register (dcte->disk_dinit + 0x06).
 * 0xE0AAD4 tests bit 10; 0xE0AAE4 masks bits 11 and 12 together.
 */
#define DISK_INT_TYPE1_PENDING  0x0400
#define DISK_INT_TYPE0_PENDING  0x1800

/*
 * Constant cell at 0x00e0ab08, passed by reference with `pea (0x10,PC)` at
 * 0xE0AAF6 (Ghidra label Disk_unrecognized_interrupt_err).
 */
static const status_$t Disk_unrecognized_interrupt_err = 0x00080026;

void DISK_INTERRUPT(void)
{
    dcte_t *dcte;
    uint16_t status;

    /* 0xE0AAC8-0xE0AAD0: the status register lives at +0x06 of the structure
     * the type-1 DCTE points at with disk_dinit (+0x34). */
    dcte = IO_$INT_CTRL.type1_dcte;
    status = *(uint16_t *)((uint8_t *)(uintptr_t)dcte->disk_dinit + 6);

    if ((status & DISK_INT_TYPE1_PENDING) != 0) {       /* 0xE0AAD4 btst #10 */
        IO_$INT_CTRL.type1_do_io(IO_$INT_CTRL.type1_dcte);   /* 0xE0AADA */
    } else if ((status & DISK_INT_TYPE0_PENDING) != 0) { /* 0xE0AAE4 */
        IO_$INT_CTRL.type0_do_io(IO_$INT_CTRL.type0_dcte);   /* 0xE0AAEA */
    } else {
        /* 0xE0AAF6: pea (0x10,PC) -> the constant cell at 0xE0AB08 holding
         * status 0x00080026, "disk unrecognized interrupt". */
        CRASH_SYSTEM(&Disk_unrecognized_interrupt_err);
    }
}
