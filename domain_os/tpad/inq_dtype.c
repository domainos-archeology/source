/*
 * tpad/inq_dtype.c - TPAD_$INQ_DTYPE implementation
 *
 * Returns the last detected pointing device type.
 *
 * Original address: 0x00E69AC4
 */

#include "tpad/tpad_internal.h"

/*
 * TPAD_$INQ_DTYPE - Inquire device type
 *
 * Returns the type of the last detected pointing device.
 * This is determined by analyzing incoming data packets.
 *
 * Returns:
 *   Device type (tpad_$dev_type_t)
 */
tpad_$dev_type_t TPAD_$INQ_DTYPE(void)
{
    return (tpad_$dev_type_t)tpad_$dev_type;
}
