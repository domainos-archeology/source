// OS_$GET_REV_INFO - Get OS revision information
// Address: 0x00e38030
// Size: 34 bytes
//
// Copies the OS revision information structure to the caller's buffer.
// The structure is 0x33 (51) 4-byte words = 204 bytes.

#include "os.h"

void OS_$GET_REV_INFO(void *buf)
{
    short i;
    uint32_t *dst = (uint32_t *)buf;
    const uint32_t *src = OS_$REV;

    // Copy 0x33 (51) 4-byte words
    for (i = 0x32; i >= 0; i--) {
        *dst++ = *src++;
    }
}
