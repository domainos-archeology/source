/*
 * DISK_$GET_ERROR_INFO - Copy the module's last-error record out
 *
 * 0x00E6C08C - 0x00E6C0A6 (28 bytes).  Verified against the disassembly on
 * 2026-09-19; the earlier emission was faithful but named the source as a
 * private address - it is DISK_$ERROR_INFO, DISK_$DATA + 0xa94 (0xE7AC60),
 * the disk_$error_info_t disk_$io_error writes.
 *
 * Argument: (0x8,A6) buffer -> 86 bytes.  `moveq #0x14` / dbf copies 21
 * longwords, then one word (0x00E6C09A - 0x00E6C0A2).
 */

#include "disk/disk_internal.h"

void DISK_$GET_ERROR_INFO(void *buffer)
{
    const uint32_t *src = (const uint32_t *)&DISK_$ERROR_INFO;
    uint32_t *dst = (uint32_t *)buffer;
    int16_t i;

    for (i = 0; i < 21; i++) {
        *dst++ = *src++;
    }
    *(uint16_t *)dst = *(const uint16_t *)src;
}
