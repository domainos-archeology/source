/*
 * io_data.c - IO Module Global Data Definitions
 *
 * This file defines the global variables used by the IO subsystem.
 *
 * Original M68K addresses:
 *   IO_$SAVED_OS_SP:    0x00E2E822 (4 bytes, void pointer)
 *   IO_$SAVED_INT_SR:   0x00EB2BF8 (2 bytes, uint16)
 */

#include "io/io_internal.h"

/*
 * ============================================================================
 * Interrupt Stack State
 * ============================================================================
 */

/*
 * IO_$SAVED_OS_SP - Saved OS stack pointer during interrupt processing
 *
 * When an interrupt switches to the dedicated interrupt stack, the
 * previous (OS) stack pointer is saved here. A non-zero value indicates
 * we are currently on the interrupt stack. Cleared when switching back
 * to the OS stack.
 *
 * Original address: 0x00E2E822
 */
void *IO_$SAVED_OS_SP = NULL;

/*
 * IO_$SAVED_INT_SR - Saved status register from interrupted context
 *
 * When switching to the interrupt stack, the SR value from the
 * interrupted exception frame is saved here so it can be examined
 * during interrupt exit processing (e.g., to determine the interrupted
 * interrupt priority level).
 *
 * Original address: 0x00EB2BF8
 */
uint16_t IO_$SAVED_INT_SR = 0;


/*
 * ============================================================================
 * Device controller tables
 * ============================================================================
 */

/*
 * IO_$DCTE_LIST - head of the singly linked list of device controller table
 * entries, 0x00E2C8B4 (map: symbol IO_$DCTE_LIST; the next map symbol,
 * IO_$IN_INIT, is at 0x00E2C8BA).  SYSBUS_$INIT walks it with
 * `movea.l (0x00e2c8b4).l,A0` / `movea.l (A0),A0`, so the cell is one 32-bit
 * pointer; the image value is 0 (the list is built at boot).
 */
dcte_t *IO_$DCTE_LIST = NULL;

/*
 * IO_$INT_CTRL - per-controller-type interrupt dispatch block, 0x00E22904.
 *
 * The map segment is "D E22904 ATBUS_ size = 2C" and it exports no interior
 * symbol, so the whole 0x2C bytes are this one module-local block; that is
 * exactly sizeof(io_int_ctrl_t).  DISK_$INTERRUPT runs with A5 pointing at it.
 * The image is all zeroes; SYSBUS_$INIT fills the slots in.
 */
io_int_ctrl_t IO_$INT_CTRL = { 0 };
#if defined(ARCH_M68K)
_Static_assert(sizeof(IO_$INT_CTRL) == 0x2C,
               "IO_$INT_CTRL: map segment ATBUS_ 0x00E22904 size = 2C");
#endif

