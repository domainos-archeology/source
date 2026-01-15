/*
 * KBD_$INQ_KBD_TYPE - Inquire keyboard type
 *
 * Returns the keyboard type string for a terminal line.
 *
 * Parameters:
 *   line_ptr   - Pointer to terminal line number
 *   type_buf   - Buffer to receive type string
 *   type_len   - Pointer to receive type string length
 *   status     - Pointer to receive status code
 *
 * Original address: 0x00e72562
 */

#include "kbd/kbd_internal.h"

void KBD_$INQ_KBD_TYPE(uint16_t *line_ptr, uint8_t *type_buf,
                       uint16_t *type_len, status_$t *status)
{
    kbd_state_t *desc;
    int16_t i;
    int16_t len;

    /* Get keyboard descriptor */
    desc = KBD_$GET_DESC(line_ptr, status);

    if (*status == status_$ok) {
        /* Copy type string */
        len = desc->kbd_type_len - 1;
        for (i = 0; i <= len; i++) {
            type_buf[i] = desc->kbd_type_str[i];
        }
        *type_len = desc->kbd_type_len;
    }
}
