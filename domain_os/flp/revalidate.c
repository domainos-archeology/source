/*
 * FLP_$REVALIDATE - Revalidate floppy disk
 *
 * This function clears the disk change flag for a floppy unit.
 * It is called after the system has detected a disk change and
 * wants to allow further operations on the new disk.
 *
 * The disk change flag is set by the hardware when the disk is
 * ejected or changed, preventing I/O until revalidation.
 */

#include "flp/flp_internal.h"

/*
 * FLP_$REVALIDATE - Clear disk change flag
 *
 * @param disk_info  Pointer to disk information structure
 *                   (offset 0x1c contains unit number)
 */
void FLP_$REVALIDATE(void *disk_info)
{
    int16_t unit;

    /* Extract unit number from disk info structure at offset 0x1c */
    unit = *(int16_t *)((uint8_t *)disk_info + 0x1c);

    /* Clear the disk change flag for this unit */
    DAT_00e7b018[unit] = 0;
}
