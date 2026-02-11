/*
 * dir_$do_op_drop_mount - Server-side handler for DROP_MOUNT op
 *
 * Server-side handler for opcode 0x5C in DIR_$DO_OP. Removes a mount
 * point entry from the directory subsystem's mount table. Searches for
 * a matching mount target UID or node ID and removes the entry by
 * swapping with the last entry in the table.
 *
 * Called by DIR_$DO_OP case 0x5C. Audit code 0x1D.
 *
 * Parameters:
 *   mount_uid  - UID of mount target to remove (from req + 0x8e)
 *   node_id    - Node ID to match (from *(uint32_t*)(req + 0x96))
 *   status_ret - Output: status code (always status_$ok)
 *
 * Original address: 0x00E533E6
 * Original size: 210 bytes
 */

#include "dir/dir_internal.h"

/*
 * Mount table offsets (relative to A5)
 */
#define DIR_MOUNT_COUNT_OFF     0x1558
#define DIR_MOUNT_COUNT16_OFF   0x155A
#define DIR_MOUNT_SRC_BASE      0x155C  /* Source UIDs: +idx*8 */
#define DIR_MOUNT_TGT_BASE      0x159C  /* Target UIDs: +idx*8 */
#define DIR_MOUNT_NODE_BASE     0x15DC  /* Node IDs: +idx*4 */

void dir_$do_op_drop_mount(uid_t *mount_uid, uint32_t node_id,
                            status_$t *status_ret)
{
    char *a5 = (char *)__A5_BASE();
    int16_t count;
    int16_t idx;

    ML_$EXCLUSION_START(&DIR_$MUTEX);

    count = *(int16_t *)(a5 + DIR_MOUNT_COUNT16_OFF) - 1;
    if (count >= 0) {
        idx = 1;
        char *tgt_ptr = a5;
        char *node_ptr = a5;

        for (int16_t i = count; i >= 0; i--) {
            int matched = 0;

            /* Match by mount target UID at offset 0x159C */
            if (mount_uid->high == *(uint32_t *)(tgt_ptr + 0x159C) &&
                mount_uid->low == *(uint32_t *)(tgt_ptr + 0x15A0)) {
                matched = 1;
            }
            /* Or match by node ID at offset 0x15DC */
            if (node_id == *(uint32_t *)(node_ptr + DIR_MOUNT_NODE_BASE)) {
                matched = 1;
            }

            if (matched) {
                int32_t mount_count = *(int32_t *)(a5 + DIR_MOUNT_COUNT_OFF);

                if (mount_count > 1) {
                    /*
                     * More than one entry - swap with last entry.
                     * Clear current slot's source UID, then copy
                     * last entry's data into this slot.
                     */
                    int32_t slot = idx * 8;
                    int32_t last;

                    /* Clear current source UID */
                    *(uint32_t *)(a5 + slot + 0x1554) = 0;

                    /* Copy target UID from last entry */
                    last = mount_count * 8;
                    *(uint32_t *)(a5 + slot + 0x1594) =
                        *(uint32_t *)(a5 + last + 0x1594);
                    *(uint32_t *)(a5 + slot + 0x1598) =
                        *(uint32_t *)(a5 + last + 0x1598);

                    /* Copy source UID from last entry */
                    last = mount_count * 8;
                    *(uint32_t *)(a5 + slot + 0x1554) =
                        *(uint32_t *)(a5 + last + 0x1554);
                    *(uint32_t *)(a5 + slot + 0x1558) =
                        *(uint32_t *)(a5 + last + 0x1558);

                    /* Copy node ID from last entry */
                    *(uint32_t *)(a5 + idx * 4 + DIR_MOUNT_NODE_BASE) =
                        *(uint32_t *)(a5 + mount_count * 4 + DIR_MOUNT_NODE_BASE);
                }

                /* Decrement mount count */
                *(int32_t *)(a5 + DIR_MOUNT_COUNT_OFF) -= 1;
                break;
            }

            idx++;
            tgt_ptr += 8;
            node_ptr += 4;
        }
    }

    ML_$EXCLUSION_STOP(&DIR_$MUTEX);

    *status_ret = status_$ok;
}
