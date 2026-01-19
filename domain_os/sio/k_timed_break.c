/*
 * SIO_$K_TIMED_BREAK - Send a timed break signal
 *
 * Sends a break signal on the serial line for the specified duration.
 * The function blocks until the break completes or a quit signal is
 * received.
 *
 * Implementation:
 * 1. Get SIO descriptor for the line
 * 2. Enable break on the line
 * 3. Wait for the specified duration (or quit signal)
 * 4. Disable break
 *
 * If the quit signal is received during the wait, the function returns
 * status_$tty_quit_signalled and restores the quit value.
 *
 * Original address: 0x00e67ee0
 */

#include "sio/sio_internal.h"

/* External references for FIM quit handling */
extern ec_$eventcount_t FIM_$QUIT_EC[];   /* At 0xe22002 */
extern int32_t FIM_$QUIT_VALUE[];         /* At 0xe222ba */
extern int16_t PROC1_$AS_ID;              /* At 0xe2060a */

/*
 * FUN_00e67e86 - Set break state (internal)
 *
 * Enables or disables break on the serial line.
 * When enable is 0xFF, break is enabled.
 * When enable is 0x00, break is disabled.
 */
extern void FUN_00e67e86(sio_desc_t *desc, uint8_t enable);

void SIO_$K_TIMED_BREAK(int16_t *line_ptr, uint16_t *duration_ptr,
                        status_$t *status_ret)
{
    sio_desc_t *desc;
    int32_t quit_wait_value;
    int8_t wait_result;
    clock_t delay;
    int16_t as_id;

    /* Get SIO descriptor for the line */
    desc = SIO_$I_GET_DESC(*line_ptr, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /* Enable break (0xFF) */
    FUN_00e67e86(desc, 0xFF);
    if (*status_ret != status_$ok) {
        return;
    }

    /*
     * Set up delay value in clock format
     * Duration is in milliseconds, convert to 4us ticks: ms * 250 = ms * 0xFA
     */
    delay.high = 0;
    delay.low = (uint16_t)(*duration_ptr * 0xFA);

    /* Calculate quit EC wait value */
    as_id = PROC1_$AS_ID;
    quit_wait_value = FIM_$QUIT_VALUE[as_id] + 1;

    /*
     * Wait for either:
     * - The delay to expire (timeout)
     * - The quit signal to be received
     *
     * TIME_$WAIT2 returns:
     *   < 0: timer expired (normal completion)
     *   >= 0: quit EC triggered
     */
    wait_result = TIME_$WAIT2(
        (uint16_t *)&(uint16_t){0},  /* delay type = relative */
        &delay,
        &FIM_$QUIT_EC[as_id * 3],    /* EC array has 12-byte stride, index = as_id * 3 */
        &quit_wait_value,
        status_ret
    );

    if (wait_result >= 0) {
        /*
         * Quit signal received - return quit status and
         * restore quit value to avoid re-triggering
         */
        *status_ret = status_$tty_quit_signalled;
        FIM_$QUIT_VALUE[as_id] = FIM_$QUIT_EC[as_id * 3].value;
    }

    /* Disable break (0x00) */
    FUN_00e67e86(desc, 0x00);
}
