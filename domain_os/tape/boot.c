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

#include "tape.h"

/*
 * TAPE_$BOOT - Check if system booted from tape
 *
 * @param status_ret  Output: status code (always status_$ok)
 * @return 0 (false) - tape boot not supported
 */
uint8_t TAPE_$BOOT(status_$t *status_ret)
{
    *status_ret = status_$ok;
    return 0;
}
