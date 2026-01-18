/*
 * PCHIST_$INIT - Initialize the PC history subsystem
 *
 * Reverse engineered from Domain/OS at address 0x00e32394
 */

#include "pchist/pchist_internal.h"

/*
 * External reference to the control structure
 * Located at 0xe2c204
 */
extern pchist_control_t PCHIST_$CONTROL;

/*
 * PCHIST_$INIT
 *
 * Initializes the PC history subsystem by setting up the
 * exclusion lock used for synchronization between processes
 * and interrupt handlers.
 */
void PCHIST_$INIT(void)
{
    ML_$EXCLUSION_INIT(&PCHIST_$CONTROL.lock);
}
