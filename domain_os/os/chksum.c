/*
 * OS_$CHKSUM - checksum request (stub)
 *
 * Original address: 0x00E6D698 (SAU2 map: OS_ code segment)
 * Size: 32 bytes (0x00E6D698 .. 0x00E6D6B7)
 *
 * The body is a stub: it clears the caller's status and enable flag and
 * returns.  The three leading arguments are never read.  A5 is saved,
 * pointed at OS_$BOOT_DEVICE (0xE82728, the OS data segment) and restored
 * without being used.
 *
 * Frame (link.w A6,-0xc; A5 saved):
 *   (0x8,A6)   param_1     never read
 *   (0xc,A6)   param_2     never read
 *   (0x10,A6)  param_3     never read
 *   (0x14,A6)  enable      pointer to a byte: `clr.b (A1)`  0x00E6D6AE
 *   (0x18,A6)  status_ret  pointer:           `clr.l (A0)`  0x00E6D6A8
 *
 * Verified against the disassembly 2026-09-27; the body was already faithful.
 */

#include "os/os_internal.h"

void OS_$CHKSUM(void *param_1, void *param_2, void *param_3,
                char *enable, status_$t *status_ret)
{
    (void)param_1;
    (void)param_2;
    (void)param_3;

    *status_ret = status_$ok;       /* 0x00E6D6A4 .. 0x00E6D6A8 */
    *enable = 0;                    /* 0x00E6D6AA .. 0x00E6D6AE */
}
