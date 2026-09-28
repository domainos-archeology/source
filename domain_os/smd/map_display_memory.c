/*
 * smd/map_display_memory.c - Map/unmap display memory (kernel-level stubs)
 *
 * Re-emitted from the image: SMD_$MAP_DISPLAY_MEMORY 0x00E700AA..0x00E700C6
 * and SMD_$UNMAP_DISPLAY_MEMORY 0x00E700C8..0x00E700E4 (30 bytes each) are
 * identical -- load A5 = 0xE82B8C, `movea.l (0x18,A6),A0` (the FIFTH
 * argument), `move.l #0x130028,(A0)`: "non-conforming and main memory blts
 * not implemented".  The first four arguments are never read.  Only the
 * SVC table references them (0x00E7BC52 / the unmap slot).
 *
 * Original addresses:
 *   SMD_$MAP_DISPLAY_MEMORY:   0x00E700AA
 *   SMD_$UNMAP_DISPLAY_MEMORY: 0x00E700C8
 */

#include "smd/smd_internal.h"

/*
 * SMD_$MAP_DISPLAY_MEMORY - Map display memory (kernel level)
 *
 * Stub function that returns unsupported status.
 * Direct display memory mapping is not implemented.
 *
 * Parameters:
 *   param_1    - Unknown (ignored)
 *   param_2    - Unknown (ignored)
 *   param_3    - Unknown (ignored)
 *   param_4    - Unknown (ignored)
 *   status_ret - Status return (always returns unsupported)
 *
 * Original implementation: returns status_$display_nonconforming_blts_unsupported
 */
void SMD_$MAP_DISPLAY_MEMORY(uint32_t param_1, uint32_t param_2,
                              uint32_t param_3, uint32_t param_4,
                              status_$t *status_ret)
{
    (void)param_1;  /* Unused */
    (void)param_2;  /* Unused */
    (void)param_3;  /* Unused */
    (void)param_4;  /* Unused */

    *status_ret = status_$display_nonconforming_blts_unsupported;
}

/*
 * SMD_$UNMAP_DISPLAY_MEMORY - Unmap display memory (kernel level)
 *
 * Stub function that returns unsupported status.
 * Direct display memory unmapping is not implemented.
 *
 * Parameters:
 *   param_1    - Unknown (ignored)
 *   param_2    - Unknown (ignored)
 *   param_3    - Unknown (ignored)
 *   param_4    - Unknown (ignored)
 *   status_ret - Status return (always returns unsupported)
 *
 * Original implementation: returns status_$display_nonconforming_blts_unsupported
 */
void SMD_$UNMAP_DISPLAY_MEMORY(uint32_t param_1, uint32_t param_2,
                                uint32_t param_3, uint32_t param_4,
                                status_$t *status_ret)
{
    (void)param_1;  /* Unused */
    (void)param_2;  /* Unused */
    (void)param_3;  /* Unused */
    (void)param_4;  /* Unused */

    *status_ret = status_$display_nonconforming_blts_unsupported;
}
