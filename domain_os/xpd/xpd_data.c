/*
 * xpd/xpd_data.c - XPD subsystem global data definitions
 *
 * Original M68K addresses:
 *   PTR_XPD_$DATA  0x00E32390 (4 bytes, last longword of the XPD code
 *                  segment: map "I E32304 XPD size = 90", i.e. the module
 *                  runs 0x00E32304..0x00E32394 and the two literal pointer
 *                  cells 0x00E3238C / 0x00E32390 sit at its tail)
 */

#include "xpd/xpd_internal.h"

/*
 * PTR_XPD_$DATA - literal pointer cell holding the base of XPD_$DATA.
 *
 * XPD_$INIT pushes its ADDRESS (together with the address of the neighbouring
 * PTR_PROC2_$DATA cell, 0x00E3238C) to MST_$WIRE_AREA, which wires the range
 * the two cells delimit.  The image value is 0x00EA5034, which the SAU2 map
 * names as segment "D68 EA5034 XPD_$DATA loaded at 1B334E, size = 4E8" -- the
 * 0x4E8 bytes XPD_$INIT zeroes.  The following segment, PROC2_$DATA, starts at
 * 0x00EA551C, which is what PTR_PROC2_$DATA holds.
 */
void *PTR_XPD_$DATA = (void *)&XPD_$DATA;
