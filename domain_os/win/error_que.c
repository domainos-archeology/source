/*
 * WIN_$ERROR_QUE - Winchester Error Queue Handler
 *
 * Simple error queue handler that just clears the result byte.
 * Winchester drives don't use error queuing.
 *
 * @param param_1  Unused parameter
 * @param param_2  Output: result byte (cleared to 0)
 */

#include "win.h"

void WIN_$ERROR_QUE(uint8_t param_1, uint8_t *param_2)
{
    (void)param_1;  /* Unused */
    *param_2 = 0;
}
