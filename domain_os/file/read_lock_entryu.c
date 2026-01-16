/*
 * FILE_$READ_LOCK_ENTRYU - Read lock entry by UID (wrapper)
 *
 * Original address: 0x00E6042E
 * Size: 64 bytes
 *
 * This is a wrapper function that calls FILE_$READ_LOCK_ENTRYUI and
 * copies the result to a smaller output buffer (26 bytes).
 *
 * Assembly analysis:
 *   - link.w A6,-0x28       ; Stack frame with 40-byte buffer
 *   - Calls FILE_$READ_LOCK_ENTRYUI at 0x00E6046E
 *   - On success, copies 26 bytes using moveq #0x19,D0 / dbf loop
 */

#include "file/file_internal.h"

/*
 * FILE_$READ_LOCK_ENTRYU - Read lock entry by UID (wrapper)
 *
 * Reads lock entry information for a file by its UID.
 * This is the public interface that returns a smaller buffer.
 *
 * Parameters:
 *   file_uid   - UID of file to query
 *   info_out   - Output buffer for lock info (26 bytes minimum)
 *   status_ret - Output: status code
 */
void FILE_$READ_LOCK_ENTRYU(uid_t *file_uid, void *info_out, status_$t *status_ret)
{
    uint8_t internal_buf[40];  /* Internal buffer for full lock info */
    int16_t i;
    uint8_t *src, *dst;

    /* Call internal function to get full lock entry info */
    FILE_$READ_LOCK_ENTRYUI(file_uid, internal_buf, status_ret);

    /* On success, copy 26 bytes to output buffer */
    if (*status_ret == status_$ok) {
        src = internal_buf;
        dst = (uint8_t *)info_out;

        /*
         * Copy 26 bytes (0x1A)
         * Using loop to match original: moveq #0x19,D0 / dbf D0w,loop
         */
        for (i = 0x19; i >= 0; i--) {
            *dst++ = *src++;
        }
    }
}
