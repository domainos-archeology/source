/*
 * rem_name/get_info.c - REM_NAME_$GET_INFO (0x00E4A690)
 *
 * Part of the REM_NAME module (SAU2 map, I 0xE4A408 size 0xB20).
 * Split out of the single name/rem_name.c (bead source-ev4k); the body
 * below is unchanged.
 */

#include "rem_name/rem_name_internal.h"

/*
 * REM_NAME_$GET_INFO - Get information about a named object
 *
 * Queries a remote naming server for detailed info about an object.
 *
 * Original address: 0x00e4a690
 * Original size: 146 bytes
 */
void REM_NAME_$GET_INFO(uint32_t net, uint32_t node, uid_t *uid,
                        void *info_ret, status_$t *status_ret)
{
    struct {
        uint32_t opcode;
        uid_t    uid;
        uint16_t flags;
    } request;

    uint8_t response[0x16a];
    int16_t resp_len;
    uint32_t *info = (uint32_t *)info_ret;
    int i;

    request.opcode = REM_NAME_OP_GET_INFO;
    request.uid.high = uid->high;
    request.uid.low = uid->low;
    request.flags = 1;

    if (!rem_name_$send_request(net, node, &request, 0x32, 0, 0x1a,
                                 response, 0x16a, &resp_len, status_ret)) {
        return;
    }

    if (resp_len >= 0x28) {
        /* Copy 22 bytes (5 longs + 1 word) of info data */
        for (i = 0; i < 5; i++) {
            info[i] = *(uint32_t *)(response + 0x12 + i * 4);
        }
        *(uint16_t *)(info + 5) = *(uint16_t *)(response + 0x12 + 20);
    } else {
        *status_ret = status_$naming_helper_sent_packets_with_errors;
    }
}
