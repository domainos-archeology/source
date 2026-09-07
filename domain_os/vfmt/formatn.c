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
 * The cell at 0x00e825e4 is a 16-byte procedure-variable descriptor whose
 * first ten bytes are a trampoline:
 *   1. Loads A0 with the address of its own 16-byte descriptor
 *   2. Jumps to the main implementation at 0x00e6b074
 *
 * Assembly at 0x00e825e4:
 *   lea (-0x2,PC),A0       ; A0 = 0x00e825e4, the descriptor itself
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
    char reserved[10];           /* +0x00: the lea/jmp trampoline */
    void (*main_func)(const char *, char *, const int16_t *, int16_t *, void *);
    uint16_t unused;             /* +0x0e: zero in the image */
} vfmt_descriptor_t;

/*
 * The descriptor as the image has it (`gsk read 0xe825e4 16`):
 *
 *   00e825e4  41 fa ff fe 4e f9 00 e6  b0 74 00 e6 ab 2a 00 00
 *
 * i.e. lea (-0x2,PC),A0 / jmp 0x00e6b074 / VFMT_$MAIN / 0.
 */
static const vfmt_descriptor_t vfmt_$formatn_descriptor_00e825e4 = {
    .reserved = {0},
    .main_func = VFMT_$MAIN,
    .unused = 0
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
