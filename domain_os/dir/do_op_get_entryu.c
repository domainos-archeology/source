/*
 * dir_$do_op_get_entryu - DO_OP handler for get directory entry
 *
 * Local handler for the GET_ENTRYU operation (opcode 0x44). This function
 * is a wrapper that calls dir_$get_entry_cached to perform the actual entry
 * lookup (which includes a per-UID cache for fast lookups). After the lookup,
 * if the entry was found on a different node than NODE_$ME and the entry
 * type is 1 (simple file), it updates the hint table to record the
 * redirect.
 *
 * Parameters:
 *   uid        - Directory UID
 *   name       - Entry name to search for
 *   name_len   - Length of name
 *   type_ret   - Output: entry type (word at resp+0x14)
 *   uid_ret    - Output: entry UID (the uid_t at resp+0x16)
 *   extra_ret  - Output: extra data (uint32_t*)
 *   status_ret - Output: status code
 *
 * Original address: 0x00E4CFFA
 * Original size: 102 bytes
 */

#include "dir/dir_internal.h"

/* ROUTE_$PORT - Network routing port reference */

void dir_$do_op_get_entryu(uid_t *uid, void *name, uint16_t name_len,
                           uint16_t *type_ret, uid_t *uid_ret,
                           uint32_t *extra_ret, status_$t *status_ret)
{
    /* Perform cached directory entry lookup */
    dir_$get_entry_cached(uid, name, name_len, type_ret, uid_ret,
                          extra_ret, status_ret);

    /* Check if the result UID points to a different node */
    if ((uid_ret->low & 0xFFFFF) != NODE_$ME) {
        /* Entry is on a different node */
        /* 0x00E4D026 `move.b (A0),D0b` reads byte 0 of the UID record, i.e.
         * the most significant byte of uid.high on this big-endian target. */
        uint16_t entry_type_byte = (uint16_t)(uint8_t)(uid_ret->high >> 24);
        if (entry_type_byte != 0) {
            /* Non-zero first byte of UID result */
            if (*type_ret == 1) {
                /* Type 1 = simple file entry */
                if (*status_ret == status_$ok) {
                    /* Update hints to record the redirect */
                    DIR_$UPDATE_HINT(uid, ROUTE_$PORT, NODE_$ME,
                                     uid_ret, *extra_ret);
                }
            }
        }
    }
}
