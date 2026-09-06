/*
 * SIO_$K_SIGNAL_WAIT - Wait for modem signal change
 *
 * Waits for specified modem signals (DCD, CTS, etc.) to match the
 * requested state. Returns when the signals match or a quit signal
 * is received.
 *
 * The function polls the inquire parameters function and waits on
 * the SIO event count which is advanced when signals change.
 *
 * Original address: 0x00e67fbe
 */

#include "sio/sio_internal.h"

/* FIM_$QUIT_EC (0xE22002) / FIM_$QUIT_VALUE (0xE222BA) come from fim/fim.h,
 * PROC1_$AS_ID (0xE2060A) from proc1/proc1.h (both via sio_internal.h). */

uint32_t SIO_$K_SIGNAL_WAIT(int16_t *line_ptr, uint32_t *signals_ptr,
                            status_$t *status_ret)
{
    sio_desc_t *desc;
    ec_$eventcount_t *wait_ecs[2];
    int32_t wait_values[2];
    uint16_t which_ec;
    int16_t as_id;
    sio_params_t params;
    uint32_t result;

    /* Get SIO descriptor for the line */
    desc = SIO_$I_GET_DESC(*line_ptr, status_ret);
    if (*status_ret != status_$ok) {
        return 0;
    }

    as_id = PROC1_$AS_ID;

    /*
     * Loop until signals match or quit received
     */
    for (;;) {
        /* Set up wait on SIO event count */
        wait_ecs[0] = &desc->ec;
        wait_values[0] = desc->ec.value + 1;

        /* Set up wait on quit event count */
        wait_ecs[1] = (ec_$eventcount_t *)&FIM_$QUIT_EC[as_id];
        wait_values[1] = FIM_$QUIT_VALUE[as_id] + 1;

        /*
         * Call inquire parameters to get current signal state
         * Parameter mask 0x180 requests signal state
         */
        ((void (*)(uint32_t, sio_params_t *, uint32_t, status_$t *))desc->inq_params)(
            desc->context,
            &params,
            0x180,
            status_ret
        );

        if (*status_ret != status_$ok) {
            return 0;
        }

        /*
         * Check if requested signals match
         * The signal bits are in params.flags1 (at offset 0x4C from desc)
         */
        result = params.flags1 & *signals_ptr;
        if (result != 0) {
            return result;
        }

        /*
         * Wait for either SIO event count or quit EC
         */
        which_ec = EC_$WAITN(wait_ecs, wait_values, 2);

        if (which_ec == 2) {
            /*
             * Quit signal received - return quit status and
             * restore quit value
             */
            *status_ret = status_$sio_quit_signalled;
            FIM_$QUIT_VALUE[as_id] = FIM_$QUIT_EC[as_id].value;
            return 0;
        }

        /* Loop and re-check signals */
    }
}
