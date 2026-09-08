/*
 * dir_$do_op_drop_mount - server-side handler for DROP_MOUNT
 *
 * Original address: 0x00E533E6
 * Original size: 210 bytes
 */

#include "dir/dir_internal.h"

/*
 * dir_$do_op_drop_mount (0x00E533E6)
 *
 * DIR_$DO_OP case 0x5C (0x00E4C954) calls it with
 * (request+0x8E, request+0x96 longword, &response+0x04).
 *
 * Walks the one-based mount tables (see DIR_MOUNT_UID_TAB_OFF in
 * dir/dir_internal.h) under DIR_$MUTEX and removes the first entry whose
 * target uid OR whose node id matches, by moving the LAST entry into its
 * slot and dropping the count.  The status is unconditionally cleared.
 *
 * Frame: `link.w A6,-0x8` (nothing in it is used).
 *
 * Parameters (A6+0x08..A6+0x10):
 *   mount_uid  - target uid to match
 *   node_id    - node id to match
 *   status_ret - out: always status_$ok
 */
void dir_$do_op_drop_mount(uid_t *mount_uid, uint32_t node_id,
                           status_$t *status_ret)
{
    char *a5 = (char *)__A5_BASE();
    int16_t remaining;
    int16_t n;

    /* 0x00E533F6 */
    ML_$EXCLUSION_START(&DIR_$MUTEX);

    /* 0x00E53404: `move.w (0x155a,A5),D0w` / `subq.w #1` / `bmi`. */
    remaining = (int16_t)(DIR_MOUNT_COUNT16(a5) - 1);

    for (n = 1; remaining >= 0; n++, remaining--) {
        int matched;

        /* 0x00E53418-0x00E53426: the target uid first ... */
        matched = (mount_uid->high ==
                       *(uint32_t *)(a5 + DIR_MOUNT_TGT_TAB_OFF + n * 8) &&
                   mount_uid->low ==
                       *(uint32_t *)(a5 + DIR_MOUNT_TGT_TAB_OFF + n * 8 + 4));
        if (!matched) {
            /* 0x00E53428: ... otherwise the node id. */
            matched = (node_id ==
                       *(uint32_t *)(a5 + DIR_MOUNT_NODE_TAB_OFF + n * 4));
        }

        if (matched) {
            /* 0x00E5342E: `cmpi.l #0x1,(0x1558,A5)` / `ble` - a table with
             * one entry left only has its count dropped. */
            if (*(int32_t *)(a5 + DIR_MOUNT_COUNT_OFF) > 1) {
                int32_t last;

                /* 0x00E53444: `clr.l (0x1554,A0)` clears only the HIGH
                 * longword of this slot's source uid; 0x00E5346C
                 * overwrites it again a few instructions later. */
                *(uint32_t *)(a5 + DIR_MOUNT_UID_TAB_OFF + n * 8) = 0;

                /* 0x00E53448-0x00E5345A: the count is re-read for every
                 * one of the three moves. */
                last = *(int32_t *)(a5 + DIR_MOUNT_COUNT_OFF);
                *(uint32_t *)(a5 + DIR_MOUNT_TGT_TAB_OFF + n * 8) =
                    *(uint32_t *)(a5 + DIR_MOUNT_TGT_TAB_OFF + last * 8);
                *(uint32_t *)(a5 + DIR_MOUNT_TGT_TAB_OFF + n * 8 + 4) =
                    *(uint32_t *)(a5 + DIR_MOUNT_TGT_TAB_OFF + last * 8 + 4);

                /* 0x00E5345E-0x00E53470 */
                last = *(int32_t *)(a5 + DIR_MOUNT_COUNT_OFF);
                *(uint32_t *)(a5 + DIR_MOUNT_UID_TAB_OFF + n * 8) =
                    *(uint32_t *)(a5 + DIR_MOUNT_UID_TAB_OFF + last * 8);
                *(uint32_t *)(a5 + DIR_MOUNT_UID_TAB_OFF + n * 8 + 4) =
                    *(uint32_t *)(a5 + DIR_MOUNT_UID_TAB_OFF + last * 8 + 4);

                /* 0x00E53474-0x00E53486: `(0x15d8,A4) = (0x15d8,A3)` with
                 * A4 = A5 + n*4 and A3 = A5 + count*4. */
                last = *(int32_t *)(a5 + DIR_MOUNT_COUNT_OFF);
                *(uint32_t *)(a5 + DIR_MOUNT_NODE_TAB_OFF + n * 4) =
                    *(uint32_t *)(a5 + DIR_MOUNT_NODE_TAB_OFF + last * 4);
            }

            /* 0x00E5348C */
            *(int32_t *)(a5 + DIR_MOUNT_COUNT_OFF) -= 1;
            break;                      /* 0x00E53490 */
        }
    }

    /* 0x00E5349C */
    ML_$EXCLUSION_STOP(&DIR_$MUTEX);

    /* 0x00E534A8: `movea.l (0x10,A6),A0` / `clr.l (A0)`. */
    *status_ret = status_$ok;
}
