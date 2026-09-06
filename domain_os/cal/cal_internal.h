/*
 * cal/cal_internal.h - Calendar subsystem internal API
 *
 * Internal declarations shared only by the CAL subsystem's implementation
 * files.  Every non-test .c file in cal/ includes this header first.
 */

#ifndef CAL_INTERNAL_H
#define CAL_INTERNAL_H

#include "cal/cal.h"

/*
 * PROC1 lock ID used to serialize access to the boot volume label block
 * (CAL_$READ_TIMEZONE / CAL_$WRITE_TIMEZONE).
 */
#define CAL_LOCK_ID 0xe

#endif /* CAL_INTERNAL_H */
