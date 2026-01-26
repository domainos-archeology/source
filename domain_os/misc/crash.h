/*
 * misc/crash.h - Crash handling utilities
 *
 * This header provides crash-related functions used by the FIM subsystem.
 */

#ifndef MISC_CRASH_H
#define MISC_CRASH_H

#include "misc/crash_system.h"

/*
 * crash_puts_string - Output string to crash console
 *
 * Low-level console output function used during crash handling.
 * Unlike CRASH_SHOW_STRING, this outputs raw strings without
 * format interpretation.
 *
 * @param str: Null-terminated string to output
 */
extern void crash_puts_string(const char *str);

/*
 * crash_putchar - Output single character to crash console
 *
 * @param c: Character to output
 */
extern void crash_putchar(char c);

/*
 * crash_puthex - Output hex value to crash console
 *
 * @param val: Value to output
 * @param digits: Number of hex digits (1-8)
 */
extern void crash_puthex(uint32_t val, int digits);

#endif /* MISC_CRASH_H */
