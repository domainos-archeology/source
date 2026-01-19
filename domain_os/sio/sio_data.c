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

/*
 * SIO_DELAY_RESTART_QUEUE_ELEM - Queue element for transmit delays
 *
 * Used by TIME_$Q_ADD_CALLBACK in SIO_$I_TSTART for timed delays
 * in the transmit buffer.
 *
 * Original address: 0x00e2dddc
 */
time_queue_elem_t SIO_DELAY_RESTART_QUEUE_ELEM;
