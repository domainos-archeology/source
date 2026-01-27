// OS_$DATA_ZERO - Zero memory efficiently
// Address: 0x00e11f42
// Size: 88 bytes
//
// Optimized memory zero that handles alignment and uses 4-byte writes
// when possible.

#include "os/os_internal.h"

void OS_$DATA_ZERO(void *ptr_v, uint32_t len)
{
    char *ptr = (char *)ptr_v;
    uint remaining;
    ushort word_count;
    char *p;

    if (len == 0) {
        return;
    }

    p = ptr;

    // Handle odd start address - write one byte to align
    if (((uintptr_t)ptr & 1) != 0) {
        *p++ = 0;
        len--;
    }

    // Save original length for final byte handling
    remaining = len;

    // Zero 4 bytes at a time if we have at least 4 bytes
    if ((int)(len - 4) >= 0) {
        uint word_iterations = (len - 4) >> 2;

        if (word_iterations < 0x10000) {
            // Use dbf loop for smaller counts (faster on 68k)
            do {
                p[0] = 0;
                p[1] = 0;
                p[2] = 0;
                p[3] = 0;
                p += 4;
                word_count = (ushort)word_iterations - 1;
                word_iterations = word_count;
            } while (word_count != 0xFFFF);
        } else {
            // Use standard loop for larger counts
            do {
                p[0] = 0;
                p[1] = 0;
                p[2] = 0;
                p[3] = 0;
                p += 4;
                word_iterations--;
            } while ((int)word_iterations >= 0);
        }
    }

    // Handle remaining 2 bytes if present
    if ((remaining & 2) != 0) {
        p[0] = 0;
        p[1] = 0;
        p += 2;
    }

    // Handle final odd byte if present
    if ((remaining & 1) != 0) {
        *p = 0;
    }
}
