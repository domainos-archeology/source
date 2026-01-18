/*
 * OS_TERM_INIT - Initialize terminal structure
 *
 * Initializes a terminal state structure by copying values from various
 * source structures and linking it to a parent descriptor. After setup,
 * calls KBD_$INIT to initialize keyboard handling for the terminal.
 *
 * Terminal structure layout (offsets in bytes, indexed as uint32_t):
 *   0x00 (index 0):  Copied from param_4[0]
 *   0x04 (index 1):  Copied from param_6[1] (offset +4)
 *   0x08 (index 2):  Copied from param_6[2] (offset +8)
 *   0x0C (index 3):  Copied from param_6[3] (offset +C)
 *   0x10 (index 4):  Copied from param_5[0]
 *   0x14 (index 5):  Copied from param_3[0]
 *   0x48 (index 18): Set to param_2 (parent descriptor pointer)
 *
 * The parent descriptor (param_2) gets a back-pointer at offset 0x2C
 * pointing to this terminal structure.
 *
 * Original address: 0x00E32A60
 * Size: 82 bytes
 */

#include "os/os_internal.h"
#include "kbd/kbd.h"

/*
 * OS_TERM_INIT - Initialize a terminal structure
 *
 * Parameters:
 *   term_state    - Terminal state structure to initialize
 *   parent_desc   - Parent descriptor (receives back-pointer at offset 0x2C)
 *   src_field_14  - Source for terminal offset 0x14
 *   src_field_00  - Source for terminal offset 0x00
 *   src_field_10  - Source for terminal offset 0x10
 *   src_fields    - Source structure for terminal offsets 0x04-0x0C
 */
void OS_TERM_INIT(uint32_t *term_state, uint32_t *parent_desc,
                  uint32_t *src_field_14, uint32_t *src_field_00,
                  uint32_t *src_field_10, uint32_t *src_fields)
{
    /* Copy individual field values to terminal structure */
    term_state[4] = *src_field_10;      /* Offset 0x10 */
    term_state[5] = *src_field_14;      /* Offset 0x14 */
    term_state[0x12] = (uint32_t)parent_desc;  /* Offset 0x48 - parent ptr */
    term_state[0] = *src_field_00;      /* Offset 0x00 */

    /* Copy three consecutive fields from src_fields (starting at offset +4) */
    term_state[1] = src_fields[1];      /* Offset 0x04 from src_fields +4 */
    term_state[2] = src_fields[2];      /* Offset 0x08 from src_fields +8 */
    term_state[3] = src_fields[3];      /* Offset 0x0C from src_fields +C */

    /* Link terminal structure back to parent descriptor at offset 0x2C */
    ((uint32_t **)parent_desc)[0x2C / 4] = term_state;

    /* Initialize keyboard handling for this terminal */
    KBD_$INIT((kbd_state_t *)term_state);
}
