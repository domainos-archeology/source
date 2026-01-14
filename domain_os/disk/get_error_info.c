/*
 * DISK_$GET_ERROR_INFO - Get disk error information
 *
 * Copies the global disk error information structure to
 * the caller's buffer.
 *
 * @param buffer  Output buffer (86 bytes: 21 longs + 1 word)
 */

#include "disk.h"

/* Global error info at 0xe7ac60 */
#define DISK_ERROR_INFO  ((uint32_t *)0x00e7ac60)

void DISK_$GET_ERROR_INFO(void *buffer)
{
    uint32_t *src = DISK_ERROR_INFO;
    uint32_t *dst = (uint32_t *)buffer;
    int16_t i;

    /* Copy 21 longs (84 bytes) */
    for (i = 0x14; i >= 0; i--) {
        *dst++ = *src++;
    }

    /* Copy final word */
    *(uint16_t *)dst = *(uint16_t *)src;
}
