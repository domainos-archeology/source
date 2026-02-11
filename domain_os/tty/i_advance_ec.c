/*
 * TTY_$I_ADVANCE_EC - Advance eventcount without dispatch
 *
 * Advances an eventcount and wakes any waiters without
 * triggering a process dispatch. Used internally by the
 * TTY subsystem for synchronization.
 *
 * Parameters:
 *   ec - Pointer to eventcount to advance
 *
 * Original address: 0x00e1aef8
 * Size: 18 bytes
 */

#include "tty/tty_internal.h"
#include "ec/ec.h"

void TTY_$I_ADVANCE_EC(m68k_ptr_t ec)
{
    EC_$ADVANCE_WITHOUT_DISPATCH((ec_$eventcount_t *)ec);
}
