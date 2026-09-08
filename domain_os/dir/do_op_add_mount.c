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
 * The mount table lives in the DIR module data area (A5 = 0xE7DC00) as
 * three ONE-BASED parallel tables - see DIR_MOUNT_UID_TAB_OFF in
 * dir/dir_internal.h.
 *
 * The cache invalidation loop is `moveq #0x6e,D0` + `dbf` (0x00E53398),
 * i.e. 0x6F = 111 iterations, over records of 0x28 bytes starting at
 * A5+0x400; it compares the uid at record+0x08 and clears record+0x00.
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
/* `move.w #0x80,-(SP)` at 0x00E532FE - the AST_$GET_COMMON_ATTRIBUTES
 * selector this site uses. */
#define DIR_CATTR_MOUNT         0x0080


/* Cache record layout (record base = A5 + 0x28*j, j = 0..110) */
#define DIR_CACHE_UID_BASE      0x400   /* record+0x00: the cached uid  */
#define DIR_CACHE_MATCH_OFF     0x408   /* record+0x08: the uid matched */
#define DIR_CACHE_STRIDE        0x28    /* record size                  */
/* `moveq #0x6e,D0` + `dbf` at 0x00E53398 = 0x6E + 1 iterations. */
#define DIR_CACHE_COUNT         (0x6E + 1)

void dir_$do_op_add_mount(uid_t *dir_uid, uid_t *mount_uid,
                           uint32_t node_id, status_$t *status_ret)
{
    uint32_t handle;
    char *a5 = (char *)__A5_BASE();
    file_$obj_loc_t desc;   /* A6-0x40, the object-location descriptor */
    int32_t count;
    ast_$common_attr_t cattr;   /* A6-0x20, 0x18 bytes */

    ACL_$ENTER_SUPER();

    /* Open directory with write access (mode=2, rights=8) */
    dir_$open_dir(dir_uid, 2, 8, &handle, status_ret);

    ACL_$EXIT_SUPER();

    if (*status_ret != status_$ok) {
        goto cleanup;
    }

    /*
     * Check for duplicate mount entry (idempotent).
     * Walk existing mount table entries.
     */
    /* 0x00E532A0-0x00E532E2.  `move.w (0x155a,A5),D0w` reads the count's
     * low word, `subq.w #1` + `bmi` skips an empty table, and the `dbf`
     * runs count times over the 1-based entries 1..count. */
    {
        int16_t remaining = (int16_t)(DIR_MOUNT_COUNT16(a5) - 1);
        int16_t n;

        for (n = 1; remaining >= 0; n++, remaining--) {
            if (node_id ==
                *(uint32_t *)(a5 + DIR_MOUNT_NODE_TAB_OFF + n * 4)) {
                if (*(uint32_t *)(a5 + DIR_MOUNT_UID_TAB_OFF + n * 8) ==
                        dir_uid->high &&
                    *(uint32_t *)(a5 + DIR_MOUNT_UID_TAB_OFF + n * 8 + 4) ==
                        dir_uid->low) {
                    if (*(uint32_t *)(a5 + DIR_MOUNT_TGT_TAB_OFF + n * 8) ==
                            mount_uid->high &&
                        *(uint32_t *)(a5 + DIR_MOUNT_TGT_TAB_OFF + n * 8 + 4) ==
                            mount_uid->low) {
                        /* 0x00E532D8: already mounted - nothing to do. */
                        goto cleanup;
                    }
                }
            }
        }
    }

    /*
     * Set up UID copy for attribute check.
     * Clear bit 6 of the flags byte in the local area.
     */
    /* 0x00E532E8: the UID goes to descriptor+0x08, where
     * AST_$GET_ATTRIBUTES reads it. */
    desc.uid = *dir_uid;
    /* 0x00E532F0 `bclr.b #0x6,(-0x23,A6)` = descriptor+0x1D. */
    desc.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

    /* Check directory attributes - verify it's not locked (0x00E53306) */
    AST_$GET_COMMON_ATTRIBUTES(&desc, DIR_CATTR_MOUNT, &cattr,
                               status_ret);
    if (*status_ret != status_$ok) {
        goto cleanup;
    }

    /* 0x00E5331A `move.b (-0x1f,A6),D0b` with the record at A6-0x20: the
     * object's sub-type, zero-extended to a word (0x00E5331E). */
    if (cattr.sub_type == 2) {
        *status_ret = status_$naming_directory_locked;   /* 0x00E53324 */
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

    /* 0x00E53362: the bumped count becomes the new entry's 1-based index. */
    *(int32_t *)(a5 + DIR_MOUNT_COUNT_OFF) = count;

    /* 0x00E53366-0x00E53372: source (mount point) UID. */
    *(uint32_t *)(a5 + DIR_MOUNT_UID_TAB_OFF + count * 8) = dir_uid->high;
    *(uint32_t *)(a5 + DIR_MOUNT_UID_TAB_OFF + count * 8 + 4) = dir_uid->low;

    /* Each of the two stores below re-reads the count out of the module
     * block (0x00E53376 / 0x00E5338A) rather than reusing D0. */
    {
        int32_t cur_count = *(int32_t *)(a5 + DIR_MOUNT_COUNT_OFF);

        /* 0x00E53382: target (mounted volume root) UID. */
        *(uint32_t *)(a5 + DIR_MOUNT_TGT_TAB_OFF + cur_count * 8) =
            mount_uid->high;
        *(uint32_t *)(a5 + DIR_MOUNT_TGT_TAB_OFF + cur_count * 8 + 4) =
            mount_uid->low;

        cur_count = *(int32_t *)(a5 + DIR_MOUNT_COUNT_OFF);
        /* 0x00E53394: `move.l D2,(0x15d8,A2)` with A2 = A5 + count*4. */
        *(uint32_t *)(a5 + DIR_MOUNT_NODE_TAB_OFF + cur_count * 4) = node_id;
    }

    /*
     * Invalidate cached directory entries that match the mounted directory UID.
     * Walk 0x6F+1 (111) cache entries at stride 0x28, comparing UIDs at +0x408.
     * If a match is found, clear the cached UID at +0x400 to UID_$NIL.
     */
    {
        int16_t j;
        char *entry = a5;
        for (j = 0; j < DIR_CACHE_COUNT; j++) {
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
    dir_$release_handle(&handle);
}
