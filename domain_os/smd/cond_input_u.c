/*
 * smd/cond_input_u.c - SMD_$COND_INPUT_U implementation
 *
 * Conditional input check - retrieves a single character if available
 * from the input queue, without blocking.
 *
 * Original address: 0x00E6FA14
 */

#include "smd/smd_internal.h"
#include "term/term.h"

/* smd_idm_event_t is now defined in smd_internal.h */

/*
 * TERM_$CONTROL's `option` argument.  It is the constant word at 0x00E6FAA6,
 * in this routine's own code region just past its `rts` (image bytes
 * "00 24"), pushed by "pea (0x22,PC)" at 0x00E6FA82.  The other two
 * TERM_$CONTROL arguments are the shared cell 0x00E6D92C, SMD_ACQ_LOCK_DATA
 * ("pea (-0x215c,PC)" and "pea (-0x2154,PC)" both resolve there).
 */
static const uint16_t smd_$cond_input_term_option = 0x0024;

/*
 * SMD_$COND_INPUT_U - Conditional input check
 *
 * Polls the input queue for available characters. Returns the first
 * keystroke character found, if any. Handles terminal control sequences
 * for special modifiers.
 *
 * Parameters:
 *   char_out - Output: receives character if available
 *
 * Returns:
 *   0xFF if a character was delivered, 0 otherwise.  The routine leaves the
 *   result in the LOW BYTE of D0 only ("move.b D2b,D0b" at 0x00E6FA9A), so
 *   the return type is a byte; the upper bytes of D0 are whatever the last
 *   callee left there.
 */
uint8_t SMD_$COND_INPUT_U(uint8_t *char_out)
{
    uint8_t result = 0;
    int8_t ctrl_sent = 0;
    uint16_t event_type;
    smd_idm_event_t event_data;
    status_$t status;

    /* Check if current process has an associated display unit */
    if (SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID] == 0) {
        return 0;
    }

    /* Poll input queue until we get a character or queue is empty */
    do {
        SMD_$GET_IDM_EVENT(&event_type, &event_data, &status);

        /* Check for error or no event */
        if (status != 0) {
            break;
        }

        /* Check if this is a keystroke event */
        if (event_type == SMD_EVTYPE_KEYSTROKE) {
            /* Check modifier flags */
            if (event_data.modifier == 0x00 || event_data.modifier == 0x0F) {
                /* Normal character or special key - return it */
                *char_out = event_data.char_code;
                result = 0xFF;
                break;
            }

            /* Handle special modifier (0x01 = control key) */
            if (ctrl_sent >= 0 && event_data.modifier == 0x01) {
                /* Send terminal control sequence for control key */
                TERM_$CONTROL((short *)&SMD_ACQ_LOCK_DATA,
                              (unsigned short *)&smd_$cond_input_term_option,
                              &SMD_ACQ_LOCK_DATA, &status);
                ctrl_sent = -1;
            }
        }
    } while (event_type != SMD_EVTYPE_NONE);

    /* 0x00E6FA9A "move.b D2b,D0b" - the byte, and only the byte. */
    return result;
}
