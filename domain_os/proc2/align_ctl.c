/*
 * PROC2_$ALIGN_CTL - Alignment control
 *
 * This function appears to be a stub or placeholder that simply
 * zeroes out its output parameters. It may have been intended to
 * control alignment checking/fault handling behavior.
 *
 * Parameters:
 *   param_1 - Unknown input (ignored)
 *   param_2 - Unknown input (ignored)
 *   result_1 - Output: set to 0 (uint16_t)
 *   result_2 - Output: set to 0 (uint32_t)
 *   status_ret - Output: set to 0 (status_$ok implied)
 *
 * Original address: 0x00e41d74
 */

#include "proc2.h"

void PROC2_$ALIGN_CTL(int param_1, int param_2, uint16_t *result_1,
                      uint32_t *result_2, status_$t *status_ret)
{
    (void)param_1;  /* unused */
    (void)param_2;  /* unused */

    *result_1 = 0;
    *result_2 = 0;
    *status_ret = 0;  /* status_$ok */
}
