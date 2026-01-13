// TTY kernel-level delay and drain functions
//
// TTY_$K_SET_DELAY - Set delay value
// Address: 0x00e67a02
//
// TTY_$K_INQ_DELAY - Inquire delay value
// Address: 0x00e67a58
//
// TTY_$K_DRAIN_OUTPUT - Wait for output buffer to drain
// Address: 0x00e67aae

#include "tty.h"

// External functions
extern void FUN_00e1aed0(tty_desc_t *tty);  // Lock TTY
extern void FUN_00e1aee4(tty_desc_t *tty);  // Unlock TTY
extern uint EC_$WAITN(void **ec_array, int *value_array, short count);

// External global variables for quit handling
extern short PROC1_$AS_ID;              // Current address space ID
extern uint8_t FIM_$QUIT_EC[];          // Quit eventcount array (12 bytes per entry)
extern uint32_t FIM_$QUIT_VALUE[];      // Quit value array (4 bytes per entry)

void TTY_$K_SET_DELAY(short *line_ptr, ushort *delay_type_ptr, short *value_ptr,
                      status_$t *status)
{
    tty_desc_t *tty;
    ushort delay_type;

    delay_type = *delay_type_ptr;

    // Validate delay type (must be in range 0-4, i.e., bit set in 0x1F)
    if (((1 << (delay_type & 0x1F)) & 0x1F) == 0) {
        *status = status_$tty_access_denied;
        return;
    }

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    // Set the delay value
    tty->delay[delay_type] = *value_ptr;
}

void TTY_$K_INQ_DELAY(short *line_ptr, ushort *delay_type_ptr, short *value_ptr,
                      status_$t *status)
{
    tty_desc_t *tty;
    ushort delay_type;

    delay_type = *delay_type_ptr;

    // Validate delay type
    if (((1 << (delay_type & 0x1F)) & 0x1F) == 0) {
        *status = status_$tty_access_denied;
        return;
    }

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    // Return the delay value
    *value_ptr = tty->delay[delay_type];
}

void TTY_$K_DRAIN_OUTPUT(short *line_ptr, status_$t *status)
{
    tty_desc_t *tty;
    uint wait_result;

    // Eventcount array for waiting
    void *ec_array[2];
    int value_array[2];

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    // Lock the TTY
    FUN_00e1aed0(tty);

    // Loop until output buffer is drained or quit is signaled
    while (1) {
        // Set up wait for output eventcount
        ec_array[0] = (void *)(uintptr_t)tty->output_ec;
        value_array[0] = ((ec_$eventcount_t *)(uintptr_t)tty->output_ec)->value + 1;

        // Set up wait for quit eventcount
        short as_id = PROC1_$AS_ID;
        ec_array[1] = &FIM_$QUIT_EC[as_id * 12];
        value_array[1] = FIM_$QUIT_VALUE[as_id] + 1;

        // Check if output buffer is already empty
        if (tty->output_head == tty->output_read) {
            break;  // Done
        }

        // Unlock TTY while waiting
        FUN_00e1aee4(tty);

        // Wait for either eventcount
        wait_result = EC_$WAITN(ec_array, value_array, 2);

        // Re-lock TTY
        FUN_00e1aed0(tty);

        // Check if quit was signaled
        if ((short)wait_result == 2) {
            *status = status_$tty_quit_signalled;
            // Update quit value to acknowledge
            FIM_$QUIT_VALUE[PROC1_$AS_ID] = *(uint32_t *)&FIM_$QUIT_EC[PROC1_$AS_ID * 12];
            break;
        }

        // Otherwise loop and check again
    }

    // Unlock the TTY
    FUN_00e1aee4(tty);
}
