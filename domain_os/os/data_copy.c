// OS_$DATA_COPY - Copy memory efficiently
// Address: 0x00e11f04
// Size: 62 bytes
//
// Optimized memory copy that uses 4-byte transfers when both source
// and destination are at least 2-byte aligned.

#include "os.h"

void OS_$DATA_COPY(const char *src, char *dst, int len)
{
    ushort remaining;
    ushort count;
    short i;
    const ulong *src_long;
    ulong *dst_long;

    remaining = (ushort)len;
    count = remaining;

    // Check if both pointers are at least 2-byte aligned
    // (odd address check - if bit 0 is 0, pointer is even/aligned)
    if (((uintptr_t)dst & 1) == 0 && ((uintptr_t)src & 1) == 0) {
        // Both are aligned - copy 4 bytes at a time
        count = remaining & 3;  // Remaining bytes after 4-byte copies
        i = (remaining >> 2) - 1;  // Number of 4-byte words to copy

        dst_long = (ulong *)dst;
        src_long = (const ulong *)src;

        if ((remaining >> 2) != 0) {
            do {
                *dst_long = *src_long;
                src_long++;
                dst_long++;
                i--;
            } while (i != -1);

            // Update byte pointers for trailing bytes
            dst = (char *)dst_long;
            src = (const char *)src_long;
        }
    }

    // Copy remaining bytes (or all bytes if not aligned) one at a time
    i = count - 1;
    if ((short)count > 0) {
        do {
            *dst = *src;
            dst++;
            src++;
            i--;
        } while (i != -1);
    }
}
