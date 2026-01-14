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

#include "win.h"

/* Controller type identifier */
static uint16_t WIN_TYPE = 0;  /* Set by FUN_00e29138 */

/* Probe function */
extern int8_t FUN_00e29138(void *type, void *addr, void *result);

status_$t WIN_$CINIT(void *controller)
{
    status_$t status = status_$ok;
    int8_t probe_result;
    uint8_t *ctrl = (uint8_t *)controller;
    uint8_t probe_data[10];
    void *jump_table_ptr;

    /* Probe for controller presence */
    probe_result = FUN_00e29138(&WIN_TYPE, ctrl + 0x34, probe_data);

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
        EC__INIT((void *)(win_data + WIN_EC_ARRAY_OFFSET +
                         (int16_t)(unit_num * WIN_UNIT_ENTRY_SIZE)));

        /* Register with DISK subsystem */
        jump_table_ptr = &WIN__JUMP_TABLE;
        DISK__REGISTER(&WIN_TYPE, &WIN_TYPE,
                       win_data + WIN_FLAGS_OFFSET,
                       win_data + WIN_DEV_TYPE_OFFSET,
                       &jump_table_ptr);
    } else {
        status = status_$io_controller_not_in_system;
    }

    return status;
}
