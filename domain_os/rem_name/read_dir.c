/*
 * rem_name/read_dir.c - REM_NAME_$READ_DIR (0x00E4A984)
 *
 * Part of the REM_NAME module (SAU2 map, I 0xE4A408 size 0xB20).
 * Split out of the single name/rem_name.c (bead source-ev4k); the body
 * below is unchanged.
 */

#include "rem_name/rem_name_internal.h"

/*
 * REM_NAME_$READ_DIR - Read directory entries
 *
 * Reads multiple directory entries from a remote naming server.
 * Uses NETBUF for large response handling.
 *
 * Original address: 0x00e4a984
 * Original size: 448 bytes
 */
void REM_NAME_$READ_DIR(uint32_t net, uint32_t node, uid_t *dir_uid,
                        uint32_t start_index, void *entries_ret,
                        uint16_t max_entries, uint16_t *count_ret,
                        status_$t *status_ret)
{
    struct {
        uint32_t opcode;
        uid_t    dir_uid;
        uint16_t flags;
        uint8_t  reserved[0x24];
        uint32_t start_index;
    } request;

    /* A6-0x58: NETBUF_$GET_HDR's physical-address output (a longword) */
    uint32_t netbuf_phys;
    uint8_t *response;
    uint32_t response_ptr;
    int16_t resp_len;
    uint8_t out_param[6];
    uint16_t entry_count;
    uint16_t i;
    uint8_t *src;
    uint8_t *dst;
    int16_t entry_type;
    uint16_t name_len;

    /* Get network buffer for large response */
    NETBUF_$GET_HDR(&netbuf_phys, &response_ptr);
    response = (uint8_t *)response_ptr;

    *count_ret = 0;

    request.opcode = REM_NAME_OP_READ_DIR;
    request.dir_uid.high = dir_uid->high;
    request.dir_uid.low = dir_uid->low;
    request.flags = 1;
    request.start_index = start_index;

    if (!rem_name_$send_request(net, node, &request, 0x36, 0, 0x0c,
                                 response, 0x200, &resp_len, status_ret)) {
        if (*status_ret != status_$naming_last_entry_in_replicated_root_returned) {
            NETBUF_$RTN_HDR(&response_ptr);
            return;
        }
        /* For shutdown status, still process any entries we got */
        if (max_entries < *(uint16_t *)(response + 0x16)) {
            *status_ret = status_$ok;
        }
    }

    /* Parse directory entries */
    entry_count = *(uint16_t *)(response + 0x16);
    if (entry_count == 0) {
        NETBUF_$RTN_HDR(&response_ptr);
        return;
    }

    src = response + 0x18;

    for (i = 0; i < entry_count && *count_ret < max_entries; i++) {
        (*count_ret)++;
        dst = (uint8_t *)entries_ret + (*count_ret - 1) * DIR_ENTRY_SIZE;

        /* Copy entry type */
        OS_$DATA_COPY(src, dst - 0x2e + DIR_ENTRY_SIZE, 2);
        entry_type = *(int16_t *)src;
        src += 2;

        /* Copy name length */
        OS_$DATA_COPY(src, dst + 2, 2);
        name_len = *(uint16_t *)src;
        src += 2;

        /* Copy name */
        OS_$DATA_COPY(src, dst + 4, name_len);

        /* Pad name with spaces to 32 chars */
        if (name_len < 32) {
            uint16_t j;
            for (j = name_len; j < 32; j++) {
                dst[4 + j] = ' ';
            }
        }

        if (entry_type == ENTRY_TYPE_NORMAL) {
            *(uint16_t *)dst = 1;
            src += name_len;
            /* Copy UID (8 bytes) and type info (4 bytes) */
            OS_$DATA_COPY(src, dst + 0x24, 8);
            src += 8;
            OS_$DATA_COPY(src, dst + 0x2c, 4);
            src += 4;
        } else if (entry_type == ENTRY_TYPE_LINK) {
            *(uint16_t *)dst = ENTRY_TYPE_LINK_ALT;
            src += name_len;
        } else {
            /* Unknown entry type - back out */
            (*count_ret)--;
            break;
        }
    }

    NETBUF_$RTN_HDR(&response_ptr);
}
