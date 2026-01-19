/*
 * SIO2681_$SET_BREAK - Set or clear break condition
 *
 * Starts or stops a break condition on the serial line by writing
 * the appropriate command to the command register.
 *
 * Original address: 0x00e1d114
 */

#include "sio2681/sio2681_internal.h"

/*
 * SIO2681_$SET_BREAK - Control break signal
 *
 * A break condition forces the TxD line low continuously, which
 * the receiver detects as a break character.
 *
 * Assembly analysis:
 *   - Acquires spin lock
 *   - If enable < 0: write start break command (0x60)
 *   - Else: write stop break command (0x70), then call SIO_$I_TSTART
 *   - Releases spin lock
 *
 * Parameters:
 *   channel - Channel structure
 *   enable  - Negative = start break, non-negative = stop break
 */
void SIO2681_$SET_BREAK(sio2681_channel_t *channel, int8_t enable)
{
    ml_$spin_token_t token;

    token = ML_$SPIN_LOCK(&SIO2681_$DATA.spin_lock);

    if (enable < 0) {
        /* Start break - write command 0x60 */
        channel->regs[SIO2681_REG_CRA] = SIO2681_$DATA.cmd_break_start;
    } else {
        /* Stop break - write command 0x70 */
        channel->regs[SIO2681_REG_CRA] = SIO2681_$DATA.cmd_break_stop;

        /* Restart transmission after break ends */
        SIO_$I_TSTART(channel->sio_desc);
    }

    ML_$SPIN_UNLOCK(&SIO2681_$DATA.spin_lock, token);
}
