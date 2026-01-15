/*
 * KBD_$PUT - Write keyboard string
 *
 * Sends a keyboard string to the terminal subsystem by calling
 * the keyboard type setting function.
 *
 * Parameters:
 *   line_ptr  - Pointer to terminal line number
 *   type_ptr  - Pointer to type data
 *   str       - String to send
 *   length    - Pointer to length of string
 *   status    - Pointer to receive status code
 *
 * Original address: 0x00e1ceac
 */

#include "kbd/kbd_internal.h"

void KBD_$PUT(uint16_t *line_ptr, uint16_t *type_ptr, void *str,
              uint16_t *length, status_$t *status)
{
    kbd_state_t *desc;

    /* Get keyboard descriptor */
    desc = KBD_$GET_DESC(line_ptr, status);

    if (*status == status_$ok) {
        /* Call type setting function */
        kbd_$set_type(desc, str, *length);
    }
}
