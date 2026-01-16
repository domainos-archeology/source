/*
 * BAT_$GET_BAT_STEP - Get BAT allocation step value
 *
 * Returns the BAT step value for a volume, which controls
 * block allocation locality.
 *
 * Original address: 0x00E3BB58
 */

#include "bat/bat_internal.h"

/*
 * BAT_$GET_BAT_STEP
 *
 * Parameters:
 *   vol_idx - Volume index (0-6)
 *
 * Returns:
 *   BAT step value for the volume
 *
 * Assembly analysis:
 *   - Multiplies vol_idx by 0x234 to index into volume array
 *   - Returns the bat_step field at offset 0x14 (relative -0x220 from partition base)
 */
uint16_t BAT_$GET_BAT_STEP(int16_t vol_idx)
{
    return bat_$volumes[vol_idx].bat_step;
}
