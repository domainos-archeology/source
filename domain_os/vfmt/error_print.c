/*
 * ERROR_$PRINT - Error message print function
 *
 * This is a Pascal-style procedure variable that wraps VFMT_$WRITE.
 * It formats and prints error messages to the console.
 *
 * Original address: 0x00E825F4
 * Original size: 10 bytes
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
 *   ERROR_$PRINT("Error code: %h%$", &error_code);
 *   ERROR_$PRINT("File not found: %a%$", &filename_len, filename);
 */

#include "vfmt/vfmt.h"
#include <stdarg.h>

/*
 * ERROR_$PRINT - Print formatted error message
 *
 * This is implemented as a direct call to VFMT_$WRITE since that's
 * what the original thunk ultimately calls.
 *
 * Parameters:
 *   format - Format string (Domain/OS VFMT format, not printf)
 *   ...    - Variadic arguments matching format specifiers
 *
 * Note: The format string uses %$ as end marker, not null termination
 * for the argument list parsing.
 */
void ERROR_$PRINT(const char *format, ...)
{
    va_list args;
    va_start(args, format);

    /*
     * In the original code, this calls through the procedure variable
     * mechanism to VFMT_$WRITE. We call VFMT_$WRITE directly here.
     *
     * TODO: The original may have passed args differently - verify
     * by checking actual call sites in the assembly.
     */
    VFMT_$WRITE(format, args);

    va_end(args);
}
