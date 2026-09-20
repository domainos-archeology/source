/*
 * TAPE_$BOOT - Check if system booted from tape
 *
 * This function checks whether the system was booted from a tape device.
 * In this configuration of Domain/OS, tape boot is not supported, so
 * the function always returns false (0) with status_$ok.
 *
 * Historically, Apollo workstations could boot from various media
 * including tape drives. This stub remains for API compatibility.
 */

#include "tape/tape_internal.h"

/*
 * TAPE_$BOOT - Check if system booted from tape
 *
 * Re-emitted from the image (0x00E32734..0x00E32742).  The routine takes
 * TWO arguments: it clears *entry_point and never touches status_ret.
 *
 * @param entry_point  Output: cleared
 * @param status_ret   Unused
 * @return 0 (false) - tape boot not supported
 */
int8_t TAPE_$BOOT(uint32_t *entry_point, status_$t *status_ret)
{
    (void)status_ret;       /* (0xC,A6): never read */
    *entry_point = 0;       /* 0x00E3273C: clr.l (A0) */
    return 0;               /* 0x00E3273E: clr.b D0b */
}
