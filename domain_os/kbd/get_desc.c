/*
 * KBD_$GET_DESC - Get keyboard descriptor
 *
 * Validates the line number and returns the keyboard descriptor.
 * If line 0 has wrong discipline, sets it to the correct one.
 *
 * Parameters:
 *   line_ptr   - Pointer to terminal line number
 *   status_ret - Pointer to receive status/descriptor
 *
 * Returns:
 *   Keyboard descriptor pointer in A0 (via extraout)
 *
 * Original address: 0x00e1aa26
 */

#include "kbd/kbd_internal.h"

/* Default discipline at 0xe1aaa6 */
static uint8_t default_discipline[] = { 0x01 };

/* Base of DTTE table - offset calculation: line * 0x38 + 0x2c */
#define DTTE_BASE       0xe2dc90
#define DTTE_ENTRY_SIZE 0x38
#define DTTE_KBD_OFFSET 0x2c
#define DTTE_DISC_OFFSET 0x34

void *KBD_$GET_DESC(uint16_t *line_ptr, status_$t *status_ret)
{
    uint16_t line = *line_ptr;
    int16_t offset;
    void *result;
    status_$t local_status;

    /* Validate line number */
    if (line > 3) {
        *status_ret = 0x000b0007;  /* status_$invalid_line_number */
        return NULL;
    }

    /* Check against max DTTE entries */
    if (line >= TERM_$MAX_DTTE) {
        *status_ret = 0x000b000d;  /* status_$requested_line_or_operation_not_implemented */
        return NULL;
    }

    *status_ret = status_$ok;

    /* Calculate offset: line * (8 - 0x38) = line * -0x30, then + 0x2c */
    offset = (int16_t)(line * DTTE_ENTRY_SIZE);

    /* Check if keyboard descriptor exists */
    result = *(void **)(DTTE_BASE + DTTE_KBD_OFFSET + offset);
    if (result == NULL) {
        *status_ret = 0x000b000d;  /* status_$requested_line_or_operation_not_implemented */
        return NULL;
    }

    /* For line 0, check discipline and set if needed */
    if (line == 0) {
        uint16_t disc = *(uint16_t *)(DTTE_BASE + DTTE_DISC_OFFSET + offset);
        if (disc != 1) {
            TERM_$SET_DISCIPLINE(line_ptr, default_discipline, &local_status);
        }
    }

    return result;
}
