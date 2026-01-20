/*
 * smd/wire_mm.c - Wire display memory
 *
 * This function is a stub that returns "unsupported" status.
 * It was likely intended for wiring display memory pages to
 * prevent paging, but this feature was not implemented.
 *
 * Original address: 0x00E6F4A0
 */

#include "smd/smd_internal.h"

/*
 * SMD_$WIRE_MM - Wire display memory
 *
 * Stub function that returns unsupported status.
 * Wire functionality for display memory is not implemented.
 *
 * Parameters:
 *   param_1    - Unknown (ignored)
 *   param_2    - Unknown (ignored)
 *   param_3    - Unknown (ignored)
 *   status_ret - Status return (always returns unsupported)
 *
 * Original implementation: returns status_$display_nonconforming_blts_unsupported
 */
void SMD_$WIRE_MM(uint32_t param_1, uint32_t param_2, uint32_t param_3,
                  status_$t *status_ret)
{
    (void)param_1;  /* Unused */
    (void)param_2;  /* Unused */
    (void)param_3;  /* Unused */

    *status_ret = status_$display_nonconforming_blts_unsupported;
}
