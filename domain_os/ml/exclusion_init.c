/*
 * ML_$EXCLUSION_INIT - Initialize an exclusion lock
 *
 * Initializes an exclusion lock structure to the unlocked state.
 * The f2/f3 pointers are set to point to the structure itself,
 * forming a self-referential list (for waiter management).
 *
 * Original address: 0x00E20E34
 */

#include "ml.h"

void ML_$EXCLUSION_INIT(ml_$exclusion_t *excl)
{
    excl->f1 = 0;           /* Clear first field */
    excl->f2 = excl;        /* Self-reference (list head) */
    excl->f3 = excl;        /* Self-reference (list tail) */
    excl->f4 = 0;           /* Event count value = 0 */
    excl->f5 = -1;          /* State = -1 means unlocked */
}
