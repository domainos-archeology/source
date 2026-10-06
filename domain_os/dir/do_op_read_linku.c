/*
 * dir_$do_op_read_linku - DO_OP handler for reading a symbolic link
 *
 * Local handler for the READ_LINKU operation (opcode 0x3E). Opens the
 * directory, looks up the named entry via dir_$find_entry, and if it's
 * a link entry (type 4), reads the link target data. If the link data
 * is inline (page index == -1), the data follows the entry header at
 * offset entry[1] + 0x0C. Otherwise, maps the link page via
 * dir_$map_link_page.
 *
 * If the entry is not a link, returns naming_not_a_link and copies
 * the entry's UID into uid_ret.
 *
 * Parameters:
 *   uid        - Directory UID
 *   name       - Entry name to read
 *   name_len   - Length of name
 *   buf_len    - Maximum target buffer length
 *   extra      - Extra data (passed through from caller)
 *   link_len_ret - Output: actual link target length
 *   uid_ret    - Output: target UID (set to NIL for links, entry UID for non-links)
 *   status_ret - Output: status code
 *
 * Original address: 0x00E4D5B4
 * Original size: 268 bytes
 */

#include "dir/dir_internal.h"

/* Status code for link target truncated */
#ifndef status_$naming_link_target_truncated
#define status_$naming_link_target_truncated  0x000E002C
#endif

void dir_$do_op_read_linku(uid_t *uid, void *name, uint16_t name_len,
                           uint16_t buf_len, uint32_t extra,
                           void *link_type_ret, uid_t *uid_ret,
                           status_$t *status_ret)
{
    /* Cast void* parameter to typed pointer for internal use */
    int16_t *link_len_ret = (int16_t *)link_type_ret;

    uint32_t local_handle;
    char found;
    uint8_t *entry;
    void *entry_ret;
    uint8_t extra1[4];
    int16_t depth_ret;   /* dir_$find_entry's depth word (`clr.w (A0)` at 0x00E4C9F0) */

    /* Initialize outputs */
    uid_ret->high = UID_$NIL.high;
    uid_ret->low = UID_$NIL.low;
    *link_len_ret = 0;

    ACL_$ENTER_SUPER();

    /* Open directory with read access, ACL right 1 (read) */
    dir_$open_dir(uid, 1, 1, &local_handle, status_ret);
    if (*status_ret != status_$ok) {
        goto done;
    }

    /* Look up the named entry */
    found = dir_$find_entry((void *)local_handle, name, name_len, 0,
                            &entry_ret, extra1, &depth_ret);
    entry = (uint8_t *)entry_ret;

    if (found < 0) {
        /* Entry found */
        if ((*entry & 7) == 4) {
            /* Link entry (type 4) */
            uint8_t *link_data;

            if (*(int16_t *)(entry + 4) == -1) {
                /* Inline link data: follows entry header at offset name_len + 0x0C */
                link_data = entry + entry[1] + 0x0C;
            } else {
                /* Link data is on a separate page */
                link_data = (uint8_t *)dir_$map_link_page(
                    (void *)local_handle, *(uint16_t *)(entry + 4));
            }

            /* Get actual link length from entry */
            *link_len_ret = *(int16_t *)(entry + 2);

            /* Truncate if buffer is too small */
            if (buf_len < *link_len_ret) {
                *link_len_ret = buf_len;
                *status_ret = status_$naming_link_target_truncated;
            }

            /* Copy link data to output buffer */
            {
                int16_t i;
                int16_t count = *link_len_ret - 1;
                if (count >= 0) {
                    int16_t idx = 1;
                    uint8_t *dest = (uint8_t *)extra;
                    do {
                        dest[idx - 1] = link_data[idx - 1];
                        idx++;
                        count--;
                    } while (count != -1);
                }
            }
        } else {
            /* Not a link entry */
            *status_ret = status_$naming_not_a_link;
            uid_ret->high = *(uint32_t *)(entry + 4);
            uid_ret->low = *(uint32_t *)(entry + 8);
        }
    } else {
        /* Entry not found */
        *status_ret = status_$naming_name_not_found;
    }

done:
    dir_$release_handle(&local_handle);
    ACL_$EXIT_SUPER();
}
