/*
 * kbd_$set_type - Set keyboard type (internal helper)
 *
 * Copies the keyboard type string and looks up the translation table.
 *
 * Parameters:
 *   state    - Pointer to keyboard state structure
 *   type_str - Type string to copy
 *   type_len - Length of type string
 *
 * Original address: 0x00e1ca8c
 */

#include "kbd/kbd_internal.h"

void kbd_$set_type(kbd_state_t *state, uint8_t *type_str, uint16_t type_len)
{
    int16_t i;
    int16_t len;
    int16_t ktt_idx;

    /* Limit type string to 2 characters */
    if (type_len > 2) {
        type_len = 2;
    }

    /* Copy type string */
    len = type_len - 1;
    for (i = 0; i <= len; i++) {
        state->kbd_type_str[i] = type_str[i];
    }
    state->kbd_type_len = type_len;

    /* Ensure second byte is zero if only one character */
    if ((int16_t)type_len < 2) {
        state->kbd_type_str[1] = 0;
    }

    /* Look up keyboard translation table */
    ktt_idx = (int16_t)type_str[1] - 0x40;
    if (ktt_idx < 0 || ktt_idx > MNK_$KTT_MAX) {
        ktt_idx = 0;
    }
    state->ktt_ptr = MNK_$KTT_PTRS[ktt_idx];
}
