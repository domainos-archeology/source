/*
 * REM_FILE_$NEIGHBORS - Check if two files are on the same remote volume
 *
 * Sends a request to a remote node to check if two files are "neighbors"
 * (on the same volume).
 *
 * Original address: 0x00E621B8
 * Size: 164 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * Neighbors request structure
 */
typedef struct {
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x10 = neighbors */
    uid_t uid1;             /* First file UID (8 bytes) */
    uid_t uid2;             /* Second file UID (8 bytes) */
    uint16_t reserved;      /* Reserved (value 3) */
    int8_t admin_flag;      /* Process admin flag */
} rem_file_neighbors_req_t;

int8_t REM_FILE_$NEIGHBORS(void *location_info, uid_t *uid1, uid_t *uid2,
                           status_$t *status)
{
    rem_file_neighbors_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;

    /* Build request */
    request.magic = 0x80;
    request.opcode = REM_FILE_OP_NEIGHBORS;
    request.uid1 = *uid1;
    request.uid2 = *uid2;
    request.reserved = 3;
    request.admin_flag = REM_FILE_PROCESS_HAS_ADMIN() ? -1 : 0;

    /* Send request */
    REM_FILE_$SEND_REQUEST(location_info, &request, 0x18,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    /* Return result byte from response if successful */
    if (*status == status_$ok) {
        return (int8_t)response[0xBC - 0xB8];  /* Result at offset in response */
    }

    return 0;
}
