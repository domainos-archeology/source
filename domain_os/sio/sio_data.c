/*
 * SIO_DATA - SIO Module Global Data
 *
 * Contains global variables and data structures for the SIO subsystem.
 */

#include "sio/sio_internal.h"

/*
 * SIO_$SPIN_LOCK - Spin lock for SIO error handling
 *
 * Protects access to error state during SIO_$I_ERR.
 *
 * Original address: 0x00e82458
 */
uint32_t SIO_$SPIN_LOCK = 0;
