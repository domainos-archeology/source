/*
 * parity/parity_data.c - PARITY subsystem global data definitions
 *
 * Original M68K addresses (both cells sit in the literal pool at the tail of
 * PARITY_$CHK, inside the map segment "I E0AE68 PARITY size = 354"; the next
 * map symbol is PARITY_$CHK_IO at 0x00E0B174):
 *   Fault_Memory_Parity_Err    0x00E0B16C  4 bytes
 *   Fault_Spurious_Parity_Err  0x00E0B170  4 bytes
 */

#include "parity/parity_internal.h"

/*
 * Fault_Memory_Parity_Err - status handed to CRASH_SYSTEM when a parity error
 * cannot be recovered (PARITY_$CHK, `pea (d,PC)` on this cell).
 *
 * Image bytes at 0x00E0B16C: 00 12 00 1E.
 */
status_$t Fault_Memory_Parity_Err = 0x0012001E;

/*
 * Fault_Spurious_Parity_Err - status handed to CRASH_SYSTEM when the parity
 * trap fires with no error latched in the hardware.
 *
 * Image bytes at 0x00E0B170: 00 12 00 21.
 */
status_$t Fault_Spurious_Parity_Err = 0x00120021;
