/*
 * misc/crash.h - Crash console helpers
 *
 * Kept as a compatibility shim: everything now lives in
 * misc/crash_system.h (CRASH_SYSTEM, CRASH_SHOW_STRING, crash_puts_string,
 * crash_putc and the crash report block).
 *
 * NOTE: this header used to declare crash_putchar() and crash_puthex(),
 * neither of which exists in the image or in this tree - the formatter at
 * 0x00E1E7C8 does the hex conversion inline.  Both declarations were removed.
 */

#ifndef MISC_CRASH_H
#define MISC_CRASH_H

#include "misc/crash_system.h"

#endif /* MISC_CRASH_H */
