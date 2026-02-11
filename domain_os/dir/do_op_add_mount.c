/*
 * dir_$do_op_add_mount - Server-side handler for ADD_MOUNT op
 *
 * Server-side handler for opcode 0x5A in DIR_$DO_OP. Adds a mount
 * point entry to the directory subsystem's mount table. The mount
 * table maps directory UIDs to mount target UIDs and node IDs.
 *
 * Process:
 * 1. Enter supervisor mode, open directory with write access
 * 2. Exit supervisor mode
 * 3. Check for duplicate mount (idempotent - return if already exists)
 * 4. Verify directory is not locked via AST_$GET_COMMON_ATTRIBUTES
 * 5. Under DIR_$MUTEX exclusion, add entry to mount table (max 8)
 * 6. Invalidate any cached directory entries matching the mounted dir
 *
 * The mount table is stored in the per-node data area (A5-relative):
 *   A5+0x1558: mount count (32-bit)
 *   A5+0x155A: mount count (16-bit, high word of above)
 *   A5+0x155C+i*8: mount point UIDs (source directory)
 *   A5+0x159C+i*8: mount target UIDs
 *   A5+0x15DC+i*4: node IDs
 *
 * The cache invalidation loop walks 0x6F (111) entries at stride 0x28
 * starting from A5+0x400, comparing UIDs at offset +8 within each entry.
 *
 * Called by DIR_$DO_OP case 0x5A. Audit code 0x1C.
 *
 * Parameters:
 *   dir_uid    - UID of directory being mounted upon
 *   mount_uid  - UID of mount target (from request at req + 0x8e)
 *   node_id    - Node ID (from request at *(uint32_t*)(req + 0x96))
 *   status_ret - Output: status code
 *
 * Original address: 0x00E5325E
 * Original size: 392 bytes
 */

#include "dir/dir_internal.h"

/*
 * Mount table constants (offsets relative to A5)
 */
#define DIR_MOUNT_COUNT_OFF     0x1558  /* Mount count (32-bit) */
#define DIR_MOUNT_COUNT16_OFF   0x155A  /* Mount count (16-bit, upper half) */
#define DIR_MOUNT_SRC_BASE      0x155C  /* Source UIDs: +idx*8 */
#define DIR_MOUNT_TGT_BASE      0x159C  /* Target UIDs: +idx*8 */
#define DIR_MOUNT_NODE_BASE     0x15DC  /* Node IDs: +idx*4 */
#define DIR_MOUNT_MAX           8       /* Maximum mount entries */

/* Cache entry layout (offsets relative to A5) */
#define DIR_CACHE_UID_BASE      0x400   /* First cache entry UID offset */
#define DIR_CACHE_MATCH_OFF     0x408   /* UID-to-match offset within cache */
#define DIR_CACHE_STRIDE        0x28    /* Cache entry size */
#define DIR_CACHE_COUNT         0x6F    /* Number of cache entries - 1 (111) */

void dir_$do_op_add_mount(uid_t *dir_uid, uid_t *mount_uid,
                           uint32_t node_id, status_$t *status_ret)
{
    uint32_t handle;
    char *a5 = (char *)__A5_BASE();
    uid_t local_uid;
    uid_t resolved_uid;     /* auStack_44 - resolved UID from handle open */
    int16_t i;
    int32_t count;
    uint8_t attr_buf[4];    /* uStack_24 area */
    char lock_state;        /* local_23 in decompilation */

    ACL_$ENTER_SUPER();

    /* Open directory with write access (mode=2, rights=8) */
    FUN_00e4ba02(dir_uid, 2, 8, &handle, status_ret);

    ACL_$EXIT_SUPER();

    if (*status_ret != status_$ok) {
        goto cleanup;
    }

    /*
     * Check for duplicate mount entry (idempotent).
     * Walk existing mount table entries.
     */
    {
        int16_t n = *(int16_t *)(a5 + DIR_MOUNT_COUNT16_OFF) - 1;
        char *src_ptr = a5 + 8;
        char *node_ptr = a5;

        for (i = n; i >= 0; i--) {
            if (node_id == *(uint32_t *)(node_ptr + DIR_MOUNT_NODE_BASE)) {
                if (*(uint32_t *)(src_ptr + 0x1554) == dir_uid->high &&
                    *(uint32_t *)(src_ptr + 0x1558) == dir_uid->low) {
                    if (*(uint32_t *)(src_ptr + 0x1594) == mount_uid->high &&
                        *(uint32_t *)(src_ptr + 0x1598) == mount_uid->low) {
                        /* Already mounted - idempotent success */
                        goto cleanup;
                    }
                }
            }
            src_ptr += 8;
            node_ptr += 4;
        }
    }

    /*
     * Set up UID copy for attribute check.
     * Clear bit 6 of the flags byte in the local area.
     */
    local_uid.high = dir_uid->high;
    local_uid.low = dir_uid->low;
    /* local_27 &= 0xBF - clear bit 6 of attribute flags byte */

    /* Check directory attributes - verify it's not locked */
    AST_$GET_COMMON_ATTRIBUTES(&local_uid, 0x80, attr_buf, status_ret);
    if (*status_ret != status_$ok) {
        goto cleanup;
    }

    /* lock_state is at the attribute result byte corresponding to local_23 */
    lock_state = ((char *)attr_buf)[3];
    if (lock_state == 0x02) {
        *status_ret = status_$naming_directory_locked;
        goto cleanup;
    }

    /* Add mount entry under mutex protection */
    ML_$EXCLUSION_START(&DIR_$MUTEX);

    count = *(int32_t *)(a5 + DIR_MOUNT_COUNT_OFF) + 1;
    if (count >= DIR_MOUNT_MAX) {
        ML_$EXCLUSION_STOP(&DIR_$MUTEX);
        *status_ret = status_$directory_is_full;
        goto cleanup;
    }

    /* Store the new mount entry */
    *(int32_t *)(a5 + DIR_MOUNT_COUNT_OFF) = count;

    /* Store source (mount point) UID */
    *(uint32_t *)(a5 + count * 8 + 0x1554) = dir_uid->high;
    *(uint32_t *)(a5 + count * 8 + 0x1558) = dir_uid->low;

    /* Store target (mounted volume root) UID */
    {
        int32_t cur_count = *(int32_t *)(a5 + DIR_MOUNT_COUNT_OFF);
        *(uint32_t *)(a5 + cur_count * 8 + 0x1594) = mount_uid->high;
        *(uint32_t *)(a5 + cur_count * 8 + 0x1598) = mount_uid->low;

        /* Store node ID */
        *(uint32_t *)(a5 + cur_count * 4 + DIR_MOUNT_NODE_BASE) = node_id;
    }

    /*
     * Invalidate cached directory entries that match the mounted directory UID.
     * Walk 0x6F+1 (111) cache entries at stride 0x28, comparing UIDs at +0x408.
     * If a match is found, clear the cached UID at +0x400 to UID_$NIL.
     */
    {
        int16_t j;
        char *entry = a5;
        for (j = DIR_CACHE_COUNT; j >= 0; j--) {
            if (*(uint32_t *)(entry + DIR_CACHE_MATCH_OFF) == dir_uid->high &&
                *(uint32_t *)(entry + DIR_CACHE_MATCH_OFF + 4) == dir_uid->low) {
                *(uint32_t *)(entry + DIR_CACHE_UID_BASE) = UID_$NIL.high;
                *(uint32_t *)(entry + DIR_CACHE_UID_BASE + 4) = UID_$NIL.low;
            }
            entry += DIR_CACHE_STRIDE;
        }
    }

    ML_$EXCLUSION_STOP(&DIR_$MUTEX);

cleanup:
    FUN_00e4b9d6(&handle);
}
