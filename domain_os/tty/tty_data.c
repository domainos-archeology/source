/*
 * tty/tty_data.c - TTY Subsystem Global Data Definitions
 *
 * Defines the global data structures for the TTY subsystem.
 * Original addresses:
 *   TTY_$SPIN_LOCK:               0xe2dd74
 *   PTR_TTY_$I_DXM_SIGNAL:        0xe1b8ac
 */

#include "tty/tty_internal.h"

/*
 * TTY_$SPIN_LOCK - Spin lock for TTY operations
 *
 * Used to protect critical sections in TTY code.
 * Zero-initialized.
 *
 * Original address: 0xe2dd74
 */
uint32_t TTY_$SPIN_LOCK = 0;

/*
 * PTR_TTY_$I_DXM_SIGNAL - Function pointer to TTY_$I_DXM_SIGNAL
 *
 * Used by DXM_$ADD_CALLBACK for signal delivery.
 *
 * Original address: 0xe1b8ac
 */
m68k_ptr_t PTR_TTY_$I_DXM_SIGNAL = (m68k_ptr_t)TTY_$I_DXM_SIGNAL;
