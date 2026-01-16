/*
 * FILE_$READ_LOCK_ENTRYUI - Read lock entry by UID (unchecked)
 *
 * Original address: 0x00E6046E
 * Size: 160 bytes
 *
 * This function reads lock entry information for a file, querying
 * either locally or remotely depending on the file's location.
 * It uses FILE_$LOCATEI to find where the file is located, then
 * calls the appropriate local or remote read function.
 *
 * If the lock was released between reads (verified by FILE_$VERIFY_LOCK_HOLDER),
 * it retries until a stable lock entry is found.
 *
 * Assembly analysis:
 *   - link.w A6,-0x34       ; Stack frame
 *   - Calls FILE_$LOCATEI to get file location
 *   - Compares location.low with NODE_$ME
 *   - If local, calls FILE_$LOCAL_READ_LOCK
 *   - If remote, calls REM_FILE_$LOCAL_READ_LOCK
 *   - Verifies result with FILE_$VERIFY_LOCK_HOLDER
 *   - Copies 34 bytes to output
 */

#include "file/file_internal.h"

/*
 * Internal lock info structure (34 bytes) - matches read_lock_entryi.c
 */
typedef struct {
    uid_t    file_uid;      /* 0x00: File UID (8 bytes) */
    uint32_t context;       /* 0x08: Lock context */
    uint32_t owner_node;    /* 0x0C: Owner's node address */
    uint16_t side;          /* 0x10: Lock side */
    uint16_t mode;          /* 0x12: Lock mode */
    uint16_t sequence;      /* 0x14: Lock sequence number */
    uint32_t holder_node;   /* 0x16: Lock holder's node */
    uint32_t holder_port;   /* 0x1A: Lock holder's port */
    uint32_t remote_node;   /* 0x1E: Remote node info */
    uint32_t remote_port;   /* 0x22: Remote port info */
} file_lock_info_internal_t;

/*
 * FILE_$READ_LOCK_ENTRYUI - Read lock entry by UID (unchecked)
 *
 * Reads lock entry information for a file. First locates the file,
 * then queries either local or remote lock tables.
 *
 * Parameters:
 *   file_uid   - UID of file to query
 *   info_out   - Output buffer for lock info (34 bytes)
 *   status_ret - Output: status code
 *
 * The function loops until:
 *   - An error occurs (other than "not locked by this process")
 *   - A valid lock entry is found and verified
 */
void FILE_$READ_LOCK_ENTRYUI(uid_t *file_uid, void *info_out, status_$t *status_ret)
{
    uid_t location;
    status_$t local_status;
    file_lock_info_internal_t lock_info;
    int16_t i;
    uint32_t *src, *dst;

    /* First, locate the file to find which node has it */
    FILE_$LOCATEI(file_uid, &location, status_ret);

    if (*status_ret != status_$ok) {
        return;
    }

    /*
     * Loop until we get a stable lock entry
     * (handles race where lock is released between read and verify)
     */
    do {
        /*
         * Check if file is local (location.low == NODE_$ME)
         * or remote (needs RPC call)
         */
        if (location.low == NODE_$ME) {
            /* Local file - query local lock table */
            FILE_$LOCAL_READ_LOCK(file_uid, &lock_info, &local_status);
        } else {
            /* Remote file - query via RPC */
            REM_FILE_$LOCAL_READ_LOCK(&location, file_uid, &lock_info, &local_status);
        }

        if (local_status != status_$ok) {
            break;
        }

        /* Verify the lock holder is still valid */
        FILE_$VERIFY_LOCK_HOLDER(&lock_info, &local_status);

        /*
         * If verification says "not locked by this process",
         * the lock was released - retry
         */
    } while (local_status == status_$file_object_not_locked_by_this_process);

    *status_ret = local_status;

    /*
     * Copy lock info to output buffer (34 bytes = 8 longs + 1 short)
     * Assembly uses: moveq #0x7,D0 / loop: move.l (A0)+,(A1)+ / dbf D0w,loop
     *                move.w (A0)+,(A1)+
     */
    src = (uint32_t *)&lock_info;
    dst = (uint32_t *)info_out;

    for (i = 7; i >= 0; i--) {
        *dst++ = *src++;
    }
    *(uint16_t *)dst = *(uint16_t *)src;
}
