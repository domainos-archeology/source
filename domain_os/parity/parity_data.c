/*
 * parity/parity_data.c - PARITY subsystem global data definitions
 *
 * Original M68K addresses (the two status cells sit in the literal pool at
 * the tail of PARITY_$CHK, inside the map segment "I E0AE68 PARITY
 * size = 354"; the next map symbol is PARITY_$CHK_IO at 0x00E0B174):
 *   Fault_Memory_Parity_Err    0x00E0B16C  4 bytes
 *   Fault_Spurious_Parity_Err  0x00E0B170  4 bytes
 *   PARITY_$DURING_DMA         0x00E2298C  the data segment "D E2298C PARITY
 *                                          size = 4"
 * The parity error state itself (map PARITY_$INFO 0xE21FE6) is
 * FIM_$WIRED_DATA.parity (fim/fim_data.c).
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

/*
 * PARITY_$DURING_DMA - the latched error happened during DMA (Domain
 * boolean, -1 / 0).  Map "D E2298C PARITY size = 4", the A5 base of
 * PARITY_$CHK and PARITY_$CHK_IO (`lea (0xe2298c).l,A5', flag at `(A5)');
 * image bytes at 0x00E2298C: 00 00 00 00.  Written at 0x00E0AF06 and
 * 0x00E0AF58 (`move.b Dn,(A5)', PARITY_$CHK) and 0x00E0B1B2 (`clr.b (A5)',
 * PARITY_$CHK_IO).  A plain object for now; TODO(source-ppgz): the one-cell
 * MODULE_DATA block PARITY_$DATA.
 */
int8_t PARITY_$DURING_DMA = 0;
