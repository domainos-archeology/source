/*
 * OS_$GET_REV_INFO - copy the OS revision block to the caller
 *
 * Original address: 0x00E38030
 * Size: 34 bytes (0x00E38030 .. 0x00E38051)
 *
 * Frame (link.w A6,0x0; A5 saved and set to 0xE78400 = OS_$REV):
 *   (0x8,A6)   buf   pointer -> A1
 *
 *   0x00E38040  lea (A5),A0 / moveq #0x32,D0     51 longwords = 204 bytes
 *   0x00E38044  move.l (A0)+,(A1)+ / dbf D0w
 *
 * Verified against the disassembly 2026-09-27; the body was already faithful.
 */

#include "os/os_internal.h"

void OS_$GET_REV_INFO(void *buf)
{
    uint32_t *dst = (uint32_t *)buf;        /* A1 */
    const uint32_t *src = OS_$REV;          /* A0 = A5 */
    int16_t i;                              /* D0w */

    /* 0x00E38042 .. 0x00E38046 */
    for (i = 0x32; i != -1; i--) {
        *dst++ = *src++;
    }
}
