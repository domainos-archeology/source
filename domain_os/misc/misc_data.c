/*
 * misc/misc_data.c - Global data for the misc "subsystem"
 *
 * Original M68K addresses:
 *   LITES_LOC: 0x00E2327C (4 bytes) - status lights display location
 *   mem_lites_row2 / mem_lites_row1: 0x00E23280 / 0x00E23282 (words, no
 *   map symbol; the rest of the 8-byte MEM_LITES segment)
 */

#include "misc/misc_internal.h"

/*
 * Status lights display-memory location; 0 means the lights are disabled.
 * Set by SET_LITES_LOC, read by the MEM_LITES process.
 */
int32_t LITES_LOC = 0;

/* 0x00E23280 / 0x00E23282: the light rows (zero in the image). */
uint16_t mem_lites_row2 = 0;
uint16_t mem_lites_row1 = 0;
