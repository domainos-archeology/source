/*
 * KBD_$GET_CHAR_AND_MODE - Get character and keyboard mode
 *
 * Fetches a character from the keyboard ring buffer and returns
 * the associated mode.
 *
 * Parameters:
 *   line_ptr - Pointer to terminal line number
 *   char_out - Pointer to receive character
 *   mode_out - Pointer to receive mode
 *   status   - Pointer to receive status code
 *
 * Returns:
 *   -1 (0xFF) if character available, 0 otherwise
 *
 * Original address: 0x00e724c4
 */

#include "kbd/kbd_internal.h"

int8_t KBD_$GET_CHAR_AND_MODE(uint16_t *line_ptr, uint8_t *char_out,
                               uint8_t *mode_out, status_$t *status)
{
    kbd_state_t *desc;
    int8_t result = 0;
    int16_t mode;

    /* Get keyboard descriptor */
    desc = KBD_$GET_DESC(line_ptr, status);

    if (*status == status_$ok) {
        result = kbd_$fetch_key(desc, char_out, &mode);
        *mode_out = KBD_$MODE_TABLE[mode];
    }

    return result;
}
