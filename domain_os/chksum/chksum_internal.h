/*
 * chksum/chksum_internal.h - CHKSUM Internal Definitions
 *
 * Internal detail of the one routine in this subsystem.  Other subsystems
 * should include chksum/chksum.h.
 */

#ifndef CHKSUM_INTERNAL_H
#define CHKSUM_INTERNAL_H

#include "chksum/chksum.h"

/*
 * Initial `dbf` counter (0x00E0A31A `move.w #0xff,D1w`).  `dbf` runs the
 * body once more after the counter reaches 0 and stops at -1, so the loop
 * body executes CHKSUM_LOOP_COUNT + 1 = 256 times.
 */
#define CHKSUM_LOOP_COUNT       0xFF

/* Words summed per loop iteration (two `add.w (A0)+,D0w`). */
#define CHKSUM_WORDS_PER_ITER   2

#endif /* CHKSUM_INTERNAL_H */
