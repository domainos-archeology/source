/*
 * OS_$GET_EC - get the shutdown eventcount
 *
 * Original address: 0x00E6D6B8 (SAU2 map: OS_ code segment)
 * Size: 62 bytes (0x00E6D6B8 .. 0x00E6D6F5)
 *
 * Registers OS_$SHUTDOWN_EC (0xE1DC00) as an EC2 eventcount and returns
 * the registered handle.  The status EC2_$REGISTER_EC1 left is then
 * normalised: bit 31 is cleared and set again exactly when the status is
 * non-zero.
 *
 * Frame (link.w A6,0x0; A2/A5 saved; A5 = 0xE82728, the OS data segment,
 * unused):
 *   (0x8,A6)   param_1   never read
 *   (0xc,A6)   ec_ret    pointer; receives the A0 result   0x00E6D6DC
 *   (0x10,A6)  status    pointer -> A2
 *
 *   0x00E6D6CA  pea (A2) / move.l #0xe1dc00,-(SP) / jsr EC2_$REGISTER_EC1
 *   0x00E6D6DE  tst.l (A2) / sne D0b            D0b = 0xFF if status != 0
 *   0x00E6D6E2  andi.b #0x7f,(A2)               clear bit 7 of the FIRST byte
 *   0x00E6D6E6  andi.b #0x80,D0b / or.b D0b,(A2)   set it when non-zero
 *
 * The byte operations act on the most significant byte of the big-endian
 * longword, i.e. bit 31 of the status_$t.
 *
 * Verified against the disassembly 2026-09-27; the body was already faithful.
 */

#include "os/os_internal.h"

void OS_$GET_EC(void *param_1, ec_$eventcount_t **ec_ret, status_$t *status)
{
    void *registered_ec;            /* A0 */
    status_$t local_status;

    (void)param_1;

    /* 0x00E6D6CA .. 0x00E6D6DC */
    registered_ec = EC2_$REGISTER_EC1(&OS_$SHUTDOWN_EC, status);
    *ec_ret = (ec_$eventcount_t *)registered_ec;

    /* 0x00E6D6DE .. 0x00E6D6EA */
    local_status = *status;
    *status = (status_$t)((uint32_t)*status & ~0x80000000u);
    if (local_status != 0) {
        *status = (status_$t)((uint32_t)*status | 0x80000000u);
    }
}
