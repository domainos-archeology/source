/*
 * mt/mt.h - MT (magnetic tape) entry points present in the SAU2 image
 *
 * The SAU2 map has "I E70A4C MT size = 4" (MT_$RELEASE) and
 * "I E70A50 MT_END_ADDR size = 4": the tape driver is configured out and
 * MT_$RELEASE is a bare `rts` followed by the word 2048.
 */

#ifndef MT_H
#define MT_H

#include "base/base.h"

/*
 * MT_$RELEASE (0x00E70A4C) - PROC2_$CLEANUP_HANDLERS_INTERNAL calls it with
 * (&const 0xFFFF word, &const 0x0001 word, &status) (0x00E3E844-0x00E3E850);
 * the `rts` reads none of them.
 */
void MT_$RELEASE(const int16_t *unit, const int16_t *mode,
                 status_$t *status_ret);

#endif /* MT_H */
