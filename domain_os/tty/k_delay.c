/*
 * TTY kernel-level delay and drain functions (re-verified 2026-09-22),
 * A5 = 0x00E8242C.
 *
 * TTY_$K_SET_DELAY, 0x00E67A02..0x00E67A56 (86 bytes):
 *   0x00E67A18  t = *delay_type_ptr; btst.l D0,#0x1F -> bit (t mod 32) of
 *               0x1F clear -> status 0x350001, return (before GET_DESC)
 *   0x00E67A28  tty = GET_DESC(*line_ptr, status); status != 0 -> return
 *   0x00E67A42  delay[t] = *value_ptr   (word at (0x40,A0,t*2))
 * TTY_$K_INQ_DELAY, 0x00E67A58..0x00E67AAC: same shape, *value_ptr = delay[t].
 *
 * TTY_$K_DRAIN_OUTPUT, 0x00E67AAE..0x00E67BB0 (260 bytes):
 *   0x00E67AC0  tty = GET_DESC(*line_ptr, status); status != 0 -> return
 *   0x00E67AE0  TTY_$I_LOCK(tty); D6 = 0x00E22002 FIM_$QUIT_EC, D7 =
 *               0x00E222BA FIM_$QUIT_VALUE, A4 = &PROC1_$AS_ID (0x00E2060A)
 *   0x00E67B0C  loop: ecs[0] = output_ec, vals[0] = *output_ec + 1;
 *               ecs[1] = &FIM_$WIRED_DATA.quit_ec[as] (as*12), vals[1] = QUIT_VALUE[as]+1
 *   0x00E67B42  output_head == output_read -> unlock, return
 *   0x00E67B4C  TTY_$I_UNLOCK; r = EC_$WAITN(ecs, vals, 2); TTY_$I_LOCK
 *   0x00E67B7A  r != 2 -> loop; else status 0x350007 and
 *               FIM_$WIRED_DATA.quit_value[as] = FIM_$WIRED_DATA.quit_ec[as].value (head longword)
 *   0x00E67BA0  TTY_$I_UNLOCK(tty)
 */

#include "tty/tty_internal.h"
#include "proc1/proc1.h"
#include "fim/fim.h"

void TTY_$K_SET_DELAY(short *line_ptr, ushort *delay_type_ptr, short *value_ptr,
                      status_$t *status)
{
    tty_desc_t *tty;
    ushort delay_type;

    delay_type = *delay_type_ptr;

    // Validate delay type (must be in range 0-4, i.e., bit set in 0x1F)
    if (((1u << (delay_type & 0x1F)) & 0x1F) == 0) {
        *status = status_$tty_invalid_option;
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
    if (((1u << (delay_type & 0x1F)) & 0x1F) == 0) {
        *status = status_$tty_invalid_option;
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
    ec_$eventcount_t *ec_array[2];
    int32_t value_array[2];

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    // Lock the TTY
    TTY_$I_LOCK(tty);

    // Loop until output buffer is drained or quit is signaled
    while (1) {
        // Set up wait for output eventcount
        ec_array[0] = (ec_$eventcount_t *)ARCH_VA_TO_PTR(tty->output_ec);   /* 0x00E67B0C */
        value_array[0] = ec_array[0]->value + 1;

        // Set up wait for quit eventcount
        /* 0x00E67B1E: as*12 into FIM_$QUIT_EC, as*4 into FIM_$QUIT_VALUE */
        short as_id = PROC1_$AS_ID;
        ec_array[1] = &FIM_$WIRED_DATA.quit_ec[as_id];
        value_array[1] = FIM_$WIRED_DATA.quit_value[as_id] + 1;

        // Check if output buffer is already empty
        if (tty->output_head == tty->output_read) {
            break;  // Done
        }

        // Unlock TTY while waiting
        TTY_$I_UNLOCK(tty);

        // Wait for either eventcount
        wait_result = EC_$WAITN(ec_array, value_array, 2);

        // Re-lock TTY
        TTY_$I_LOCK(tty);

        // Check if quit was signaled
        if ((short)wait_result == 2) {
            *status = status_$tty_quit_while_waiting_for_input;
            /* 0x00E67B88..0x00E67B9A: acknowledge by snapshotting the head
             * longword of the quit eventcount (the earlier emission indexed
             * FIM_$WIRED_DATA.quit_ec[as*12], which is off by the element size) */
            FIM_$WIRED_DATA.quit_value[PROC1_$AS_ID] = (uint32_t)FIM_$WIRED_DATA.quit_ec[PROC1_$AS_ID].value;
            break;
        }

        // Otherwise loop and check again
    }

    // Unlock the TTY
    TTY_$I_UNLOCK(tty);
}
