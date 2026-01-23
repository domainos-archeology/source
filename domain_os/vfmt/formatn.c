/*
 * vfmt/formatn.c - VFMT_$FORMATN implementation
 *
 * This file contains the VFMT_$FORMATN function which is a wrapper
 * around VFMT_$MAIN that handles syscall entry and varargs setup.
 *
 * There are two entry points in the original binary:
 *   0x00e6b074 - Main implementation
 *   0x00e825e4 - Thunk that sets A0 to function table pointer
 *
 * The thunk at 0x00e825e4 is the syscall entry point which:
 *   1. Loads A0 with a pointer to the function table
 *   2. Jumps to the main implementation at 0x00e6b074
 *
 * Assembly at 0x00e825e4:
 *   lea (-0x2,PC),A0       ; A0 = 0x00e825e2 (function table)
 *   jmp 0x00e6b074.l       ; Jump to main implementation
 *
 * Assembly at 0x00e6b074:
 *   link.w A6,0x0
 *   move.l A5,-(SP)
 *   movea.l A0,A5          ; Save function table pointer
 *   pea (0x18,A6)          ; Push varargs pointer
 *   move.l (0x14,A6),-(SP) ; Push out_len
 *   move.l (0x10,A6),-(SP) ; Push max_len
 *   move.l (0xc,A6),-(SP)  ; Push buf
 *   move.l (0x8,A6),-(SP)  ; Push format
 *   movea.l (0xa,A5),A0    ; Load VFMT_$MAIN address from table
 *   jsr (A0)               ; Call VFMT_$MAIN
 *   ...
 */

#include "vfmt/vfmt_internal.h"
#include <stdarg.h>

/*
 * VFMT function table
 *
 * The original code uses A5 to point to a function table that contains:
 *   +0x00: Reserved
 *   +0x0a: Pointer to VFMT_$MAIN
 *
 * This allows the formatting functions to be vectored through a table.
 */
typedef struct {
    char reserved[10];           /* +0x00: Reserved bytes */
    void (*main_func)(const char *, char *, int16_t *, int16_t *, void *);
} vfmt_table_t;

/* Default function table pointing to VFMT_$MAIN */
static vfmt_table_t vfmt_table = {
    .reserved = {0},
    .main_func = VFMT_$MAIN
};

/*
 * VFMT_$FORMATN - Format string with numeric output
 *
 * This is the primary syscall entry point for string formatting.
 * It wraps VFMT_$MAIN with varargs handling.
 *
 * Parameters:
 *   format  - Format string (Pascal-style 1-based indexing)
 *   buf     - Output buffer
 *   max_len - Pointer to maximum buffer length
 *   out_len - Pointer to receive actual output length
 *   ...     - Format arguments
 *
 * The varargs are passed as an array of 4-byte values that VFMT_$MAIN
 * consumes as needed based on the format specifiers.
 */
void VFMT_$FORMATN(const char *format, char *buf, int16_t *max_len,
                   int16_t *out_len, ...)
{
    va_list ap;

    va_start(ap, out_len);

    /*
     * Call VFMT_$MAIN with the varargs pointer.
     * In the original code, this is done by:
     *   1. Computing the stack address of the first vararg
     *   2. Passing it as the 'args' parameter to VFMT_$MAIN
     *
     * The va_list type varies by platform, but we can use it
     * directly since VFMT_$MAIN treats it as a pointer to an
     * array of 4-byte values.
     */
    VFMT_$MAIN(format, buf, max_len, out_len, (void *)ap);

    va_end(ap);
}

/*
 * VFMT_$WRITE - Write formatted output to console
 *
 * Formats a string and writes it directly to the console/terminal.
 * This is a convenience wrapper around VFMT_$FORMATN that outputs
 * to the system console.
 *
 * Original address: 0x00e6afe2
 */
void VFMT_$WRITE(const char *format, ...)
{
    char buf[256];
    int16_t max_len = 256;
    int16_t out_len = 0;
    va_list ap;

    va_start(ap, format);
    VFMT_$MAIN(format, buf, &max_len, &out_len, (void *)ap);
    va_end(ap);

    /*
     * TODO: Output buf[0..out_len] to console
     * This would typically call TERM_$WRITE or similar.
     */
}

/*
 * VFMT_$WRITEN - Write formatted output with length limit
 *
 * Like VFMT_$WRITE but with a maximum output length.
 *
 * Original address: 0x00e6b0a4
 */
void VFMT_$WRITEN(const char *format, int16_t max_len, ...)
{
    char buf[256];
    int16_t actual_max;
    int16_t out_len = 0;
    va_list ap;

    actual_max = (max_len < 256) ? max_len : 256;

    va_start(ap, max_len);
    VFMT_$MAIN(format, buf, &actual_max, &out_len, (void *)ap);
    va_end(ap);

    /*
     * TODO: Output buf[0..out_len] to console
     * This would typically call TERM_$WRITE or similar.
     */
}
