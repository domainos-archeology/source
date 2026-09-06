/*
 * misc/misc_data.c - Global data for the misc "subsystem"
 *
 * Original M68K addresses:
 *   LITES_LOC: 0x00E2327C (4 bytes) - status lights display location
 */

#include "misc/misc_internal.h"

/*
 * Status lights display-memory location; 0 means the lights are disabled.
 * Set by SET_LITES_LOC, read by the MEM_LITES process.
 */
int32_t LITES_LOC = 0;
