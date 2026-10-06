/*
 * ct/ct.h - CT (cartridge tape) entry points present in the SAU2 image
 *
 * The SAU2 map has "I E70AF4 CT size = 4" (CT_$PROC_START and CT_$RELEASE
 * at 0x00E70AF4) and "I E70AF8 CT_END_ADDR size = 4": the cartridge tape
 * driver is configured out and CT_$RELEASE is a bare `rts` followed by the
 * word 2048.
 */

#ifndef CT_H
#define CT_H

#include "base/base.h"

/*
 * CT_$RELEASE (0x00E70AF4) - PROC2_$CLEANUP_HANDLERS_INTERNAL calls it with
 * (&const 0 word, &const 0xFF00 word, &status) (0x00E3E87E-0x00E3E88A);
 * the `rts` reads none of them.
 */
void CT_$RELEASE(const int16_t *unit, const int16_t *mode,
                 status_$t *status_ret);

#endif /* CT_H */
