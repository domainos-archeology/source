/*
 * SIO_$I_ERR - Get and clear pending receive errors
 *
 * Returns the first pending error status code and clears it from
 * the pending error mask. Error priority (highest to lowest):
 *   - Parity error (bit 1) -> status_$sio_parity_error
 *   - Framing error (bit 0) -> status_$sio_framing_error
 *   - Hardware error (bit 2) -> status_$sio_hardware_error
 *   - Overrun error (bit 3) -> status_$sio_overrun_error
 *   - Break detected (bit 4) -> status_$sio_break_detected
 *
 * The check_all parameter controls which errors to check:
 *   - If negative (< 0): check all error bits (0x1F)
 *   - If non-negative: check only bits 0x18 (overrun + break)
 *
 * Original address: 0x00e67d9c
 */

#include "sio/sio_internal.h"

/* SIO spin lock for error handling - external */
extern uint32_t SIO_$SPIN_LOCK;

status_$t SIO_$I_ERR(sio_desc_t *desc, int8_t check_all)
{
    ml_$spin_token_t token;
    uint32_t error_mask;
    uint32_t errors;

    /* No pending errors - return OK */
    if (desc->pending_int == 0) {
        return status_$ok;
    }

    /* Acquire spin lock */
    token = ML_$SPIN_LOCK(&SIO_$SPIN_LOCK);

    /* Determine which error bits to check */
    if (check_all < 0) {
        error_mask = 0x1F;  /* All error bits */
    } else {
        error_mask = 0x18;  /* Only overrun (bit 3) and break (bit 4) */
    }

    /* Get matching errors and clear them from pending */
    errors = desc->pending_int & error_mask;
    desc->pending_int &= ~errors;

    /* Release spin lock */
    ML_$SPIN_UNLOCK(&SIO_$SPIN_LOCK, token);

    /*
     * Return status code for highest priority error
     * Priority order: parity > framing > hardware > overrun > break
     */
    if (errors & 0x02) {
        return status_$sio_parity_error;      /* Bit 1 */
    }
    if (errors & 0x01) {
        return status_$sio_framing_error;     /* Bit 0 */
    }
    if (errors & 0x04) {
        return status_$sio_hardware_error;    /* Bit 2 */
    }
    if (errors & 0x08) {
        return status_$sio_overrun_error;     /* Bit 3 */
    }
    if (errors & 0x10) {
        return status_$sio_break_detected;    /* Bit 4 */
    }

    return status_$ok;
}
