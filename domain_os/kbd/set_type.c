/*
 * kbd_$set_type - Install a keyboard type string and its translation table
 *
 * Copies at most two bytes of the type string into the descriptor, records
 * the (clamped) length, zeroes the second byte when fewer than two were
 * given, and selects the translation table from the SECOND byte of the
 * caller's string: MNK_$KTT_PTRS[byte - 0x40], or entry 0 when that index
 * is negative or above MNK_$KTT_MAX.  The second byte is read from the
 * caller's buffer regardless of the length passed.
 *
 * Module-local (no map symbol); a Pascal function whose callers reserve a
 * result slot they never read (D0 is left holding the table index).
 *
 * Parameters (frame 0x00E1CA94..0x00E1CA9C):
 *   0x08 state    - the descriptor (A0)
 *   0x0C type_str - the string (A1)
 *   0x10 type_len - word by value (D0w)
 *
 * Original address: 0x00e1ca8c, 114 bytes
 *
 *   00e1caa0  moveq #2,D1 / cmp.w D1w,D0w / bls / move.w D1w,D0w   ; UNSIGNED clamp to 2
 *   00e1caa8  D1 = len - 1; bmi -> skip copy
 *   00e1caae  D2 = 0; loop: (0x40,A0,D2) = (A1,D2); D2++; dbf D1
 *   00e1cabc  move.w D0w,(0x44,A0)                                 ; kbd_type_len
 *   00e1cac0  cmpi.w #2,D0w / bge; clr.b (0x41,A0)
 *   00e1caca  clr.w D1w / move.b (0x1,A1),D1b / subi.w #0x40,D1w
 *   00e1cad4  move.w D1w,D0w / bmi -> 0
 *   00e1cad8  cmp.w (0x00e273fc).l,D0w / ble -> keep              ; MNK_$KTT_MAX, SIGNED
 *   00e1cae0  clr.w D0w
 *   00e1cae2  ext.l / lsl.l #2 / move.l (0xe273dc,D1),(0x34,A0)   ; MNK_$KTT_PTRS[i]
 */

#include "kbd/kbd_internal.h"

void kbd_$set_type(kbd_state_t *state, uint8_t *type_str, uint16_t type_len)
{
    uint16_t len;           /* D0w */
    int16_t count;          /* D1w */
    uint16_t i;             /* D2w */
    int16_t ktt_idx;        /* D1w / D0w from 0x00E1CACA */

    /* 0x00E1CAA0..0x00E1CAA6 */
    len = type_len;
    if (len > 2) {
        len = 2;
    }

    /* 0x00E1CAA8..0x00E1CAB8 */
    count = (int16_t)(len - 1);
    if (count >= 0) {
        for (i = 0; count >= 0; count--, i++) {
            state->kbd_type_str[i] = type_str[i];
        }
    }

    /* 0x00E1CABC */
    state->kbd_type_len = len;

    /* 0x00E1CAC0..0x00E1CAC6: signed compare */
    if ((int16_t)len < 2) {
        state->kbd_type_str[1] = 0;
    }

    /* 0x00E1CACA..0x00E1CAE0 */
    ktt_idx = (int16_t)((uint16_t)type_str[1] - 0x40);
    if (ktt_idx < 0 || ktt_idx > MNK_$KTT_MAX) {
        ktt_idx = 0;
    }

    /* 0x00E1CAE2..0x00E1CAEE */
    state->ktt_ptr = MNK_$KTT_PTRS[ktt_idx];
}
