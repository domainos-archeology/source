/*
 * WIN_$CINIT - Winchester Controller Initialization
 *
 * Initializes a Winchester disk controller and registers it with
 * the DISK subsystem. Called during system startup for each
 * Winchester controller found.
 *
 * @param controller  Controller info structure
 * @return            status_$ok on success, error code on failure
 */

#include "win/win_internal.h"
#include "prom/prom.h"

/*
 * 0x00E303D6: the constant controller-type word.  io_$probe receives its
 * address (`pea (0x7c,PC)` at 0x00E30358, PC = 0x00E3035A) and
 * DISK_$REGISTER receives it TWICE - once as `type` and once as
 * `controller` (0x00E303BE pea's it, and 0x00E303C2 duplicates the pushed
 * pointer).  It is read-only: nothing in the image writes it.
 */
static const uint16_t WIN_TYPE = 0;

status_$t WIN_$CINIT(void *controller)
{
    status_$t status = status_$ok;
    int8_t probe_result;
    uint8_t *ctrl = (uint8_t *)controller;
    uint8_t probe_data[10];
    void *jump_table_ptr;

    /* Probe for controller presence */
    probe_result = io_$probe((void *)&WIN_TYPE, ctrl + 0x34, probe_data);

    if (probe_result < 0) {
        /* Controller found - initialize data area */
        uint8_t *win_data = WIN_DATA_BASE;
        uint16_t unit_num;

        /* Save controller info */
        *(void **)win_data = controller;
        *(uint32_t *)(win_data + WIN_BASE_ADDR_OFFSET) = *(uint32_t *)(ctrl + 0x34);
        *(uint16_t *)(win_data + WIN_DEV_TYPE_OFFSET) = *(uint16_t *)(ctrl + 0x3c);

        /* Set flags - bits 3 and 5 */
        *(uint8_t *)(win_data + WIN_FLAGS_OFFSET) |= 0x28;

        /* Initialize event counter for this unit */
        unit_num = *(uint16_t *)(ctrl + 6);
        EC_$INIT((void *)(win_data + WIN_EC_ARRAY_OFFSET +
                         (int16_t)(unit_num * WIN_UNIT_ENTRY_SIZE)));

        /*
         * Register with the DISK subsystem.  0x00E303AA stores
         * WIN_DATA_BASE+0x10 into the A6-0x14 cell whose address is passed
         * as `jump_table`; `units` is the flags word at +0x0A and `flags`
         * the device-type word at +0x08 - both read as WORDS by
         * DISK_$REGISTER (0x00E3D9CA / 0x00E3D9D6).
         */
        jump_table_ptr = win_data + WIN_JUMP_TABLE_OFFSET;
        DISK_$REGISTER((uint16_t *)&WIN_TYPE, (uint16_t *)&WIN_TYPE,
                       (uint16_t *)(void *)(win_data + WIN_FLAGS_OFFSET),
                       (uint16_t *)(void *)(win_data + WIN_DEV_TYPE_OFFSET),
                       &jump_table_ptr);
    } else {
        status = status_$io_controller_not_in_system;
    }

    return status;
}
