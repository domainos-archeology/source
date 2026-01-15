/*
 * KBD_$SET_KBD_TYPE - Set keyboard type
 *
 * Sets the keyboard type for a terminal line.
 *
 * Parameters:
 *   line_ptr - Pointer to terminal line number
 *   type_ptr - Pointer to type data
 *   type_len - Pointer to type string length
 *   status   - Pointer to receive status code
 *
 * Original address: 0x00e72524
 */

#include "kbd/kbd_internal.h"

void KBD_$SET_KBD_TYPE(uint16_t *line_ptr, void *type_ptr,
                       uint16_t *type_len, status_$t *status)
{
    kbd_state_t *desc;

    /* Get keyboard descriptor */
    desc = KBD_$GET_DESC(line_ptr, status);

    if (*status == status_$ok) {
        kbd_$set_type(desc, type_ptr, *type_len);
    }
}
