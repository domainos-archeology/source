/*
 * VFMT_$WRITE10 - Error message print function
 *
 * This is a Pascal-style procedure variable that wraps VFMT_$WRITE.
 * It formats and prints error messages to the console.
 *
 * Original address: 0x00E825F4
 * Original size: 10 bytes
 *
 * Naming: the SR10.2 SAU2 link map (sau2-maps/domain_os.10.2.map) puts a
 * 0x10-byte data segment VFMT_$WRITEN at 0x00E825F4 carrying three aliases
 * for this one descriptor -- VFMT_$WRITE2, VFMT_$WRITE5 and VFMT_$WRITE10
 * (the SR2/SR5/SR10 compatibility spellings, exactly as the sibling
 * descriptor at 0x00E825E4 carries VFMT_$FORMAT2/5/10 and
 * VFMT_$ENCODE2/5/10).  This file was previously called ERROR_$PRINT, a
 * guessed name: no map of any SAU build contains an ERROR_$PRINT symbol.
 * VFMT_$WRITE10 is used here because this image is SR10.2; the segment
 * name VFMT_$WRITEN is already taken by the code thunk at 0x00E6B0A4
 * (vfmt/sau2/writen.s).
 *
 * Assembly structure (procedure variable thunk):
 *   00e825f4    lea (-0x2,PC),A0       ; A0 = address of this descriptor
 *   00e825f8    jmp 0x00e6b0a4.l       ; Jump to VFMT_$WRITEN
 *
 * The structure at 0xe825f4:
 *   offset 0x00: 41 fa ff fe           (lea instruction)
 *   offset 0x04: 4e f9 00 e6 b0 a4     (jmp instruction)
 *   offset 0x0a: 00 e6 af e2           (pointer to VFMT_$WRITE)
 *
 * When called:
 *   1. Sets A0 to point to the descriptor (0xe825f4)
 *   2. Jumps to VFMT_$WRITEN
 *   3. VFMT_$WRITEN extracts the function pointer at offset 10 (VFMT_$WRITE)
 *   4. Calls VFMT_$WRITE with the passed arguments
 *
 * This is essentially a direct alias to VFMT_$WRITE for error printing.
 * The thunk pattern allows for indirection through the procedure variable
 * mechanism used in Pascal.
 *
 * Usage:
 *   VFMT_$WRITE10("Error code: %h%$", &error_code);
 *   VFMT_$WRITE10("File not found: %a%$", &filename_len, filename);
 */

#include "vfmt/vfmt_internal.h"
#include <stdarg.h>

/*
 * VFMT_$WRITE10 - Print formatted error message
 *
 * Flattening of the procedure-variable dispatch.  Verified against the
 * image: the descriptor at 0x00E825F4 is
 *
 *   00e825f4  41 fa ff fe   lea (-0x2,%pc),%a0   ; A0 = 0x00E825F4
 *   00e825f8  4e f9 00 e6 b0 a4  jmp VFMT_$WRITEN ; the thunk, 0x00E6B0A4
 *   00e825fe  00 e6 af e2   installed routine    = VFMT_$WRITE
 *   00e82602  00 00         unused
 *
 * and the thunk (vfmt/sau2/writen.s) calls that routine with exactly two
 * arguments: the caller's first stack argument (the format pointer) and the
 * *address* of the caller's second stack argument.  So the whole chain is
 *
 *   VFMT_$WRITE10(format, a1, a2, ...) -> VFMT_$WRITE(format, &a1)
 *
 * which is what the va_list below stands in for: on m68k a va_list is the
 * address of the next stack argument, so `ap` after va_start(ap, format) is
 * the same pointer the thunk computes with `pea (0xc,%sp)`.
 *
 * Parameters:
 *   format - Format string (Domain/OS VFMT format, not printf)
 *   ...    - Variadic arguments matching format specifiers
 *
 * Note: The format string uses %$ as end marker, not null termination
 * for the argument list parsing.
 */
void VFMT_$WRITE10(const char *format, ...)
{
    va_list args;
    va_start(args, format);

    VFMT_$WRITE(format, (void *)args);

    va_end(args);
}
