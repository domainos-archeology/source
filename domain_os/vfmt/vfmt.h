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
void VFMT_$MAIN(const char *format, char *buf, const int16_t *max_len,
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
 * VFMT_$WRITE - Format and write to the console
 *
 * Formats into a 200-byte buffer and writes the result to terminal line 1
 * in chunks of at most 100 characters.  `args` is the address of the
 * caller's argument list, not a value: the routine is reached through the
 * VFMT_$WRITEN procedure-variable thunk (vfmt/sau2/writen.s), which
 * computes that address from its own caller's stack.
 *
 * Original address: 0x00e6afe2
 */
void VFMT_$WRITE(const char *format, void *args);

/*
 * VFMT_$WRITEN - Procedure-variable dispatch thunk (hand-written assembly)
 *
 * Takes the descriptor address in A0, not on the stack, so it has no C
 * calling convention and no C prototype.  See vfmt/sau2/writen.s.
 *
 * Original address: 0x00e6b0a4
 */

/*
 * VFMT_$WRITE10 - Print formatted error message
 *
 * A 16-byte procedure-variable descriptor at 0x00e825f4 whose installed
 * routine is VFMT_$WRITE; calls reach it through the VFMT_$WRITEN thunk.
 * Used throughout the kernel to print error messages to the console.
 *
 * Parameters:
 *   format - Format string (Domain/OS VFMT format)
 *   ...    - Variadic arguments matching format specifiers
 *
 * Original address: 0x00e825f4
 */
void VFMT_$WRITE10(const char *format, ...);

#endif /* VFMT_H */
