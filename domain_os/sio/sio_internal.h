/*
 * SIO - Serial I/O Module (Internal)
 *
 * Internal definitions for the SIO subsystem.
 * Contains private functions and data structures.
 */

#ifndef SIO_INTERNAL_H
#define SIO_INTERNAL_H

#include "ml/ml.h"
#include "sio/sio.h"
#include "term/term.h"
#include "time/time.h"

/*
 * ============================================================================
 * Internal Data
 * ============================================================================
 */

/*
 * SIO_$SPIN_LOCK - Spin lock for SIO operations
 *
 * Used to protect SIO error handling operations.
 *
 * Original address: 0x00e82458
 */
extern uint32_t SIO_$SPIN_LOCK;

/*
 * SIO_DELAY_RESTART_QUEUE_ELEM - Queue element storage for delay restart
 *
 * Used by TIME_$Q_ADD_CALLBACK for transmit delays.
 *
 * Original address: 0x00e2dddc
 */
extern time_queue_elem_t SIO_DELAY_RESTART_QUEUE_ELEM;

/*
 * ============================================================================
 * Internal Function Declarations
 * ============================================================================
 */

/*
 * SIO_DELAY_RESTART - Callback for transmit delay completion
 *
 * Called by the time subsystem when a transmit delay expires.
 * Restarts transmission.
 *
 * Parameters:
 *   args - Pointer to array containing SIO descriptor pointer
 *
 * Returns:
 *   Result from SIO_$I_TSTART
 *
 * Original address: 0x00e1c690
 */
uint16_t SIO_DELAY_RESTART(sio_desc_t **args);

/*
 * FUN_00e67e86 - Set break state
 *
 * Internal function to set or clear break state on the serial line.
 *
 * Parameters:
 *   desc - SIO descriptor
 *   enable - Enable break (non-zero) or disable (zero)
 *
 * Original address: 0x00e67e86
 */
void FUN_00e67e86(sio_desc_t *desc, uint8_t enable);

/*
 * ============================================================================
 * External References from Other Modules
 * ============================================================================
 */

/*
 * From TERM module
 */
extern int16_t TERM_$MAX_DTTE;
extern dtte_t DTTE[];

/*
 * TERM_$GET_REAL_LINE - Map virtual line to real line
 *
 * Original address: 0x00e1a9d4
 */
extern int16_t TERM_$GET_REAL_LINE(int16_t line_num, status_$t *status_ret);

/*
 * From FIM module
 */
extern ec_$eventcount_t FIM_$QUIT_EC[];
extern int32_t FIM_$QUIT_VALUE[];

/*
 * From PROC1 module
 */
extern int16_t PROC1_$AS_ID;

/*
 * From TTY module
 */

/*
 * TTY_$I_ENABLE_CRASH_FUNC - Enable crash handler on TTY
 *
 * Enables special key handling for crash/break on console.
 *
 * Parameters:
 *   tty_desc - TTY descriptor
 *   key_code - Key code for crash (0x1B = ESC typically)
 *   flags - Flags (0xFF typically)
 *
 * Original address: 0x00e67292
 */
extern void TTY_$I_ENABLE_CRASH_FUNC(void *tty_desc, uint16_t key_code,
                                     uint8_t flags);

#endif /* SIO_INTERNAL_H */
