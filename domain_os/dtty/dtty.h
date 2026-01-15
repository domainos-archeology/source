/*
 * dtty/dtty.h - Display TTY Module
 *
 * Provides display terminal handling for Domain/OS.
 */

#ifndef DTTY_H
#define DTTY_H

#include "base/base.h"

/*
 * DTTY_$USE_DTTY - Display TTY active flag
 *
 * Non-zero when the display TTY is active and should be used
 * for console output.
 */
extern char DTTY_$USE_DTTY;

#endif /* DTTY_H */
