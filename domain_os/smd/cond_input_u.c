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

/*
 * IDM event data structure for SMD_$GET_IDM_EVENT
 */
typedef struct smd_idm_event_t {
    uint32_t    timestamp;              /* 0x00: Event timestamp */
    uint32_t    field_04;               /* 0x04: Unknown */
    uint16_t    field_08;               /* 0x08: Unknown */
    uint8_t     char_code;              /* 0x0A: Character code */
    uint8_t     modifier;               /* 0x0B: Modifier flags */
} smd_idm_event_t;

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
 *   Non-zero (0xFF) if character available, 0 otherwise
 */
uint16_t SMD_$COND_INPUT_U(uint8_t *char_out)
{
    uint8_t result = 0;
    uint8_t got_char = 0;
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
                TERM_$CONTROL(&SMD_ACQ_LOCK_DATA, &SMD_ACQ_LOCK_DATA,
                              &SMD_ACQ_LOCK_DATA, &status);
                ctrl_sent = -1;
            }
        }
    } while (event_type != SMD_EVTYPE_NONE);

    return (uint16_t)result | ((uint16_t)got_char << 8);
}
