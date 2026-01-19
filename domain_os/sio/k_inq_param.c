/*
 * SIO_$K_INQ_PARAM - Inquire serial port parameters
 *
 * Returns the current serial port parameters. Copies the parameters
 * from the SIO descriptor and optionally calls the driver's inquire
 * function for dynamic state (modem signals, etc.).
 *
 * Original address: 0x00e6832a
 */

#include "sio/sio_internal.h"

/* Copy sio_params_t structure */
static void copy_params(sio_params_t *dst, sio_params_t *src)
{
    dst->flags1 = src->flags1;
    dst->flags2 = src->flags2;
    dst->break_mask = src->break_mask;
    dst->baud_rate = src->baud_rate;
    dst->char_size = src->char_size;
    dst->stop_bits = src->stop_bits;
    dst->parity = src->parity;
}

void SIO_$K_INQ_PARAM(int16_t *line_ptr, sio_params_t *params_ret,
                      uint32_t *mask_ptr, status_$t *status_ret)
{
    sio_desc_t *desc;

    /* Get SIO descriptor for the line */
    desc = SIO_$I_GET_DESC(*line_ptr, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /* Copy current parameters from descriptor */
    copy_params(params_ret, &desc->params);

    /*
     * Call driver's inquire function to get dynamic state
     * (modem signals, actual baud rate, etc.)
     */
    ((void (*)(uint32_t, sio_params_t *, uint32_t, status_$t *))desc->inq_params)(
        desc->context,
        params_ret,
        *mask_ptr,
        status_ret
    );
}
