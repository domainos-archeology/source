/*
 * rem_name/read_rep.c - REM_NAME_$READ_REP (0x00E4AB44)
 *
 * Part of the REM_NAME module (SAU2 map, I 0xE4A408 size 0xB20).
 * Split out of the single name/rem_name.c (bead source-ev4k); the body
 * below is unchanged.
 */

#include "rem_name/rem_name_internal.h"

/*
 * REM_NAME_$READ_REP - Read replication information
 *
 * Reads replica location entries from a remote naming server.
 *
 * Original address: 0x00e4ab44
 * Original size: 232 bytes
 */
void REM_NAME_$READ_REP(uint32_t net, uint32_t node, uid_t *dir_uid,
                        uint32_t start_index, void *rep_ret,
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
    uint32_t *dst;

    NETBUF_$GET_HDR(&netbuf_phys, &response_ptr);
    response = (uint8_t *)response_ptr;

    *count_ret = 0;

    request.opcode = REM_NAME_OP_READ_REP;
    request.dir_uid.high = dir_uid->high;
    request.dir_uid.low = dir_uid->low;
    request.flags = 1;
    request.start_index = start_index;

    if (!rem_name_$send_request(net, node, &request, 0x36, 0, 0x0e,
                                 response, 0x200, &resp_len, status_ret)) {
        if (*status_ret != status_$naming_last_entry_in_replicated_root_returned) {
            NETBUF_$RTN_HDR(&response_ptr);
            return;
        }
    }

    entry_count = *(uint16_t *)(response + 0x16);
    if (entry_count == 0) {
        NETBUF_$RTN_HDR(&response_ptr);
        return;
    }

    src = response + 0x18;

    for (i = 0; i < entry_count && *count_ret < max_entries; i++) {
        (*count_ret)++;
        dst = (uint32_t *)((uint8_t *)rep_ret + (*count_ret - 1) * REP_ENTRY_SIZE);

        /* Copy 18-byte replica entry (4 longs + 1 word) */
        dst[0] = *(uint32_t *)(src + 0);
        dst[1] = *(uint32_t *)(src + 4);
        dst[2] = *(uint32_t *)(src + 8);
        dst[3] = *(uint32_t *)(src + 12);
        *(uint16_t *)(dst + 4) = *(uint16_t *)(src + 16);

        src += REP_ENTRY_SIZE;
    }

    NETBUF_$RTN_HDR(&response_ptr);
}
