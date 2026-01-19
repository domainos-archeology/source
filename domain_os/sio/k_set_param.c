/*
 * SIO_$K_SET_PARAM - Set serial port parameters
 *
 * Sets one or more serial port parameters based on the change mask.
 * Parameters include baud rate, character size, stop bits, parity,
 * flow control settings, etc.
 *
 * The function:
 * 1. Gets the SIO descriptor for the line
 * 2. Copies current parameters to local storage
 * 3. Compares new parameters to current - only updates changed ones
 * 4. Validates parameter values
 * 5. Calls the driver's set_params function
 * 6. Updates the descriptor with new parameters on success
 *
 * Change mask bits:
 *   0x0001, 0x0002 - Baud rate
 *   0x0004 - Character size (0-3)
 *   0x0008 - Stop bits (1-3)
 *   0x0010 - Parity (0-3)
 *   0x0020 - Software flow control
 *   0x0040 - CTS flow control
 *   0x0200 - RTS assertion
 *   0x0400 - DTR assertion
 *   0x0800 - DCD hangup
 *   0x1000 - Receive error notification
 *   0x2000 - Break character mask
 *   0x4000 - DCD notification
 *
 * Original address: 0x00e680ac
 */

#include "sio/sio_internal.h"

/* Copy sio_params_t structure (5 uint32_t + 1 uint16_t = 22 bytes) */
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

void SIO_$K_SET_PARAM(int16_t *line_ptr, sio_params_t *params,
                      uint32_t *change_mask_ptr, status_$t *status_ret)
{
    sio_desc_t *desc;
    uint32_t change_mask;
    sio_params_t local_params;

    /* Get SIO descriptor for the line */
    desc = SIO_$I_GET_DESC(*line_ptr, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    change_mask = *change_mask_ptr;

    /* Copy current parameters to local storage */
    copy_params(&local_params, &desc->params);

    /*
     * Process each parameter bit - only update if the new value differs
     * from the current value. Clear the change bit if no actual change.
     */

    /* Bit 5 (0x20) - Software flow control enable (flags1 bit 0) */
    if (change_mask & 0x20) {
        if ((local_params.flags1 & 0x01) == (params->flags1 & 0x01)) {
            change_mask &= ~0x20;
        } else {
            local_params.flags1 = (local_params.flags1 & ~0x01) | (params->flags1 & 0x01);
        }
    }

    /* Bit 6 (0x40) - CTS flow control enable (flags1 bit 3) */
    if (change_mask & 0x40) {
        if ((local_params.flags1 & 0x08) == (params->flags1 & 0x08)) {
            change_mask &= ~0x40;
        } else {
            local_params.flags1 = (local_params.flags1 & ~0x08) | (params->flags1 & 0x08);
        }
    }

    /* Bit 11 (0x800) - DCD hangup (flags2 bit 2) */
    if (change_mask & 0x800) {
        if ((local_params.flags2 & 0x04) == (params->flags2 & 0x04)) {
            change_mask &= ~0x800;
        } else {
            local_params.flags2 = (local_params.flags2 & ~0x04) | (params->flags2 & 0x04);
        }
    }

    /* Bit 12 (0x1000) - Receive error notification (flags2 bit 3) */
    if (change_mask & 0x1000) {
        if ((local_params.flags2 & 0x08) == (params->flags2 & 0x08)) {
            change_mask &= ~0x1000;
        } else {
            local_params.flags2 = (local_params.flags2 & ~0x08) | (params->flags2 & 0x08);
        }
    }

    /* Bit 9 (0x200) - RTS assertion (flags2 bit 0) */
    if (change_mask & 0x200) {
        if ((local_params.flags2 & 0x01) == (params->flags2 & 0x01)) {
            change_mask &= ~0x200;
        } else {
            local_params.flags2 = (local_params.flags2 & ~0x01) | (params->flags2 & 0x01);
        }
    }

    /* Bit 10 (0x400) - DTR assertion (flags2 bit 1) */
    if (change_mask & 0x400) {
        if ((local_params.flags2 & 0x02) == (params->flags2 & 0x02)) {
            change_mask &= ~0x400;
        } else {
            local_params.flags2 = (local_params.flags2 & ~0x02) | (params->flags2 & 0x02);
        }
    }

    /* Bit 14 (0x4000) - DCD notification (flags2 bit 6) */
    if (change_mask & 0x4000) {
        if ((local_params.flags2 & 0x40) == (params->flags2 & 0x40)) {
            change_mask &= ~0x4000;
        } else {
            local_params.flags2 = (local_params.flags2 & ~0x40) | (params->flags2 & 0x40);
        }
    }

    /*
     * Bits 0-1 (0x03) - Baud rate
     * Validate: both halves must be <= 16
     */
    if (change_mask & 0x03) {
        if ((params->baud_rate >> 16) <= 16 &&
            (params->baud_rate & 0xFFFF) <= 16) {
            if (local_params.baud_rate == params->baud_rate) {
                change_mask &= ~0x03;
            } else {
                local_params.baud_rate = params->baud_rate;
            }
        } else {
            *status_ret = status_$sio_invalid_param;
            return;
        }
    }

    /* Bit 2 (0x04) - Character size (0-3) */
    if (change_mask & 0x04) {
        if (params->char_size > 3) {
            *status_ret = status_$sio_invalid_param;
            return;
        }
        if (local_params.char_size == params->char_size) {
            change_mask &= ~0x04;
        } else {
            local_params.char_size = params->char_size;
        }
    }

    /* Bit 3 (0x08) - Stop bits (1-3) */
    if (change_mask & 0x08) {
        if (params->stop_bits < 1 || params->stop_bits > 3) {
            *status_ret = status_$sio_invalid_param;
            return;
        }
        if (local_params.stop_bits == params->stop_bits) {
            change_mask &= ~0x08;
        } else {
            local_params.stop_bits = params->stop_bits;
        }
    }

    /* Bit 4 (0x10) - Parity (0-3) */
    if (change_mask & 0x10) {
        if (params->parity > 3) {
            *status_ret = status_$sio_invalid_param;
            return;
        }
        if (local_params.parity == params->parity) {
            change_mask &= ~0x10;
        } else {
            local_params.parity = params->parity;
        }
    }

    /* Bit 13 (0x2000) - Break character mask (must have bits 7-6 = 0) */
    if (change_mask & 0x2000) {
        if (params->break_mask & 0xC0) {
            *status_ret = status_$sio_invalid_param;
            return;
        }
        if (local_params.break_mask == params->break_mask) {
            change_mask &= ~0x2000;
        } else {
            local_params.break_mask = params->break_mask;
        }
    }

    /*
     * If any changes remain, call the driver's set_params function
     */
    if (change_mask != 0) {
        ((void (*)(uint32_t, sio_params_t *, uint32_t, status_$t *))desc->set_params)(
            desc->context,
            &local_params,
            change_mask,
            status_ret
        );

        /* Update descriptor parameters on success */
        if (*status_ret == status_$ok) {
            copy_params(&desc->params, &local_params);
        }
    }
}
