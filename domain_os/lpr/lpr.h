/*
 * lpr/lpr.h - LPR (line printer) entry points present in the SAU2 image
 *
 * The SAU2 map has only "I E70A40 LPR_ASM size = 4" (LPR_$PROC_START and
 * LPR_$RELEASE at 0x00E70A40) and "I E70A44 LPR_END_ADDR size = 4": the
 * printer driver is configured out of this kernel and LPR_$RELEASE is a
 * bare `rts` followed by a zero pad word.
 */

#ifndef LPR_H
#define LPR_H

#include "base/base.h"

/*
 * LPR_$RELEASE (0x00E70A40) - PROC2_$CLEANUP_HANDLERS_INTERNAL calls it with
 * (&const 0 word, &status) (0x00E3E82C-0x00E3E834); the `rts` reads neither
 * and leaves the status untouched.
 */
void LPR_$RELEASE(const int16_t *unit, status_$t *status_ret);

#endif /* LPR_H */
