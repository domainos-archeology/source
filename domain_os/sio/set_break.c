/*
 * sio_$set_break - Set or clear break state on serial line
 *
 * Under spin lock, modifies the line status flags at byte offset 0x75:
 *   enable < 0 (0xFF): clear bit 0, set bit 3 (break active)
 *   enable >= 0 (0x00): clear bit 3 (break inactive)
 * Then releases the spin lock and calls through the output_start
 * vtable entry (function pointer at offset 0x48 in the descriptor).
 *
 * Parameters:
 *   desc   - SIO descriptor
 *   enable - Negative (0xFF) to enable break, non-negative (0x00) to disable
 *
 * Original address: 0x00E67E86
 * Size: 90 bytes
 */

#include "sio/sio_internal.h"

void sio_$set_break(sio_desc_t *desc, uint8_t enable)
{
    uint16_t token;

    /* Acquire TTY module spin lock (A5-based in original) */
    token = ML_$SPIN_LOCK(&SIO_$SPIN_LOCK);

    if ((int8_t)enable < 0) {
        /* Enable break: clear bit 0, set bit 3 at offset 0x75 */
        *((uint8_t *)desc + 0x75) &= ~0x01;
        *((uint8_t *)desc + 0x75) |= 0x08;
    } else {
        /* Disable break: clear bit 3 at offset 0x75 */
        *((uint8_t *)desc + 0x75) &= ~0x08;
    }

    ML_$SPIN_UNLOCK(&SIO_$SPIN_LOCK, token);

    /* Call output start through vtable at offset 0x48
     * Passes: desc->line_id (offset 0x00), enable flag */
    ((void (*)(uint32_t, uint8_t))(*(void **)((char *)desc + 0x48)))
        (*(uint32_t *)desc, enable);
}
