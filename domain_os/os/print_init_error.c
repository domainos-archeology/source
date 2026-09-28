/*
 * OS_$PRINT_INIT_ERROR - print an initialisation message on the console
 *
 * Original address: 0x00E6D1CC (SAU2 map: OS_ code segment)
 * Size: 24 bytes (0x00E6D1CC .. 0x00E6D1E3); its constant cell follows at
 * 0x00E6D1E4.
 *
 * Frame (link.w A6,-0x4):
 *   (0x8,A6)   msg   the format string, '%$'-terminated
 *
 *   0x00E6D1D0  pea (0x12,PC)          -> 0x00E6D1E4, a zero longword
 *   0x00E6D1D4  move.l (SP),-(SP)      the same address again
 *   0x00E6D1D6  move.l (0x8,A6),-(SP)  msg
 *   0x00E6D1DA  jsr 0x00e825f4         VFMT_$WRITE10 (the VFMT_$WRITEN
 *                                      procedure-variable trampoline)
 *   0x00E6D1E0  unlk A6                no stack cleanup
 *
 * VFMT_$WRITE10 formats `msg` with the two argument pointers that follow
 * it on the stack; both point at the zero cell.
 *
 * Verified against the disassembly 2026-09-27; the body was already faithful.
 */

#include "os/os_internal.h"
#include "vfmt/vfmt.h"

/* 0x00E6D1E4: 00 00 00 00 - the argument cell both pointers name */
static const uint32_t os_$print_init_error_zero_00e6d1e4 = 0;

void OS_$PRINT_INIT_ERROR(const char *msg)
{
    /* 0x00E6D1D0 .. 0x00E6D1DA */
    VFMT_$WRITE10(msg, &os_$print_init_error_zero_00e6d1e4,
                  &os_$print_init_error_zero_00e6d1e4);
}
