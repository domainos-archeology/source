/*
 * FILE_$READ_LOCK_ENTRY - Read lock entry information (wrapper)
 *
 * Original address: 0x00E608EC
 * Size: 80 bytes
 *
 * This is a wrapper function that calls FILE_$READ_LOCK_ENTRYI and
 * copies the result to a smaller output buffer (26 bytes).
 *
 * Assembly analysis:
 *   - link.w A6,-0x30       ; Stack frame
 *   - Copies input UID to local buffer
 *   - Calls FILE_$READ_LOCK_ENTRYI at 0x00E6093C
 *   - On success, copies 26 bytes (0x1A) from internal buffer to output
 *     using moveq #0x19,D0 and dbf loop
 */

#include "file/file_internal.h"

/*
 * Lock entry information structure (external format)
 * Size: 26 bytes (0x1A)
 *
 * This is the format returned by FILE_$READ_LOCK_ENTRY to callers.
 * Contains essential lock information without internal implementation details.
 */
typedef struct {
    uid_t    file_uid;      /* 0x00: File UID (8 bytes) */
    uint32_t context;       /* 0x08: Lock context */
    uint32_t owner_node;    /* 0x0C: Owner's node address */
    uint16_t side;          /* 0x10: Lock side (0=reader, 1=writer) */
    uint16_t mode;          /* 0x12: Lock mode */
    uint16_t sequence;      /* 0x14: Lock sequence number */
    uint32_t holder_node;   /* 0x16: Lock holder's node */
    uint32_t holder_port;   /* 0x1A: Lock holder's port */
    uint32_t remote_node;   /* 0x1E: Remote node info */
    uint32_t remote_port;   /* 0x22: Remote port info */
} file_lock_info_t;

/*
 * FILE_$READ_LOCK_ENTRY - Read lock entry information
 *
 * Reads information about a lock on a file. This is a wrapper that
 * calls the internal iteration function and copies a subset of the
 * returned data.
 *
 * Parameters:
 *   file_uid   - UID of file to query
 *   index      - Pointer to iteration index (starts at 1, updated on return)
 *   info_out   - Output buffer for lock info (26 bytes minimum)
 *   status_ret - Output: status code
 *
 * The index parameter is used for iteration:
 *   - Pass 1 to get the first lock entry
 *   - On return, index is updated to the next value
 *   - Returns 0xFFFF when no more entries
 */
void FILE_$READ_LOCK_ENTRY(uid_t *file_uid, uint16_t *index,
                            void *info_out, status_$t *status_ret)
{
    uid_t local_uid;
    file_lock_info_internal_t internal_buf;  /* Internal buffer for FILE_$READ_LOCK_ENTRYI */
    int16_t i;
    uint8_t *src, *dst;

    /* Copy input UID to local buffer */
    local_uid.high = file_uid->high;
    local_uid.low = file_uid->low;

    /* Call internal function to get lock entry info */
    FILE_$READ_LOCK_ENTRYI(&local_uid, index, &internal_buf, status_ret);

    /* On success, copy 26 bytes to output buffer */
    if (*status_ret == status_$ok) {
        src = (uint8_t *)&internal_buf;
        dst = (uint8_t *)info_out;

        /*
         * Copy 26 bytes (0x1A)
         * Using loop to match original: moveq #0x19,D0 / dbf D0w,loop
         * which copies 26 bytes (0x19 + 1)
         */
        for (i = 0x19; i >= 0; i--) {
            *dst++ = *src++;
        }
    }
}
