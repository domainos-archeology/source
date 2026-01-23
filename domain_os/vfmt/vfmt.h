/*
 * vfmt/vfmt.h - Variable Format String Functions
 *
 * This module provides printf-like formatting functions used throughout
 * the kernel. The format syntax is similar to printf but with some
 * Domain/OS-specific extensions.
 *
 * Format specifiers:
 *   %a   - ASCII string (with length parameter)
 *   %$   - End of format/argument list marker
 *   %d   - Decimal integer
 *   %h   - Hexadecimal integer
 *   %o   - Octal integer
 *   %x   - Hexadecimal with repeat count
 *   %t   - Tab to position
 *   %(n) - Repeat group n times
 *   %)   - End of repeat group
 *   %%   - Literal percent sign
 *   %wd  - Word (16-bit) decimal
 *
 * Modifiers:
 *   l - Left justify
 *   r - Right justify (default)
 *   u - Uppercase
 *   z - Zero-strip leading zeros
 *   s - Signed
 *   p - Plus sign for positive
 *   j - No leading zeros
 */

#ifndef VFMT_H
#define VFMT_H

#include "base/base.h"

/*
 * VFMT_$MAIN - Main format string processor
 *
 * Processes a format string and arguments, writing formatted output
 * to a buffer. This is the primary entry point for all kernel formatting.
 *
 * Parameters:
 *   format     - Format string (1-indexed Pascal-style)
 *   buf        - Output buffer
 *   max_len    - Pointer to maximum buffer length
 *   out_len    - Pointer to receive actual output length
 *   args       - Pointer to argument list
 *
 * The format string uses Pascal-style 1-based indexing. Arguments are
 * passed as an array of 4-byte values, consumed in order as format
 * specifiers are processed.
 *
 * Original address: 0x00e6ab2a
 */
void VFMT_$MAIN(const char *format, char *buf, int16_t *max_len,
                int16_t *out_len, void *args);

/*
 * VFMT_$FORMATN - Wrapper for VFMT_$MAIN with thunk support
 *
 * This is a thin wrapper that sets up the function table pointer
 * before calling VFMT_$MAIN. It's the standard entry point for
 * syscall-based formatting.
 *
 * Parameters are passed through to VFMT_$MAIN, with additional
 * variadic arguments following.
 *
 * Original addresses: 0x00e6b074 (implementation), 0x00e825e4 (thunk)
 */
void VFMT_$FORMATN(const char *format, char *buf, int16_t *max_len,
                   int16_t *out_len, ...);

/*
 * VFMT_$WRITE - Write formatted output to console
 *
 * Formats a string and writes it directly to the console/terminal.
 *
 * Original address: 0x00e6afe2
 */
void VFMT_$WRITE(const char *format, ...);

/*
 * VFMT_$WRITEN - Write formatted output with length limit
 *
 * Like VFMT_$WRITE but with a maximum output length.
 *
 * Original address: 0x00e6b0a4
 */
void VFMT_$WRITEN(const char *format, int16_t max_len, ...);

#endif /* VFMT_H */
