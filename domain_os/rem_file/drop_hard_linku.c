/*
 * REM_FILE_$DROP_HARD_LINKU - Remove a hard link on a remote file server
 *
 * Removes a directory entry (hard link) from a remote directory.
 *
 * Original address: 0x00E62588
 * Size: 154 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * Drop hard link request structure
 */
typedef struct {
    uint16_t msg_type;      /* Set to 1 by SEND_REQUEST */
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x28 = drop hard link */
    uid_t dir_uid;          /* Directory UID (8 bytes) */
    char name[32];          /* Entry name (padded with spaces) */
    uint16_t name_len;      /* Name length */
    uint16_t flags2;        /* Additional flags */
    uint16_t flags;         /* Flags (value 3) */
    uint8_t force_flag;     /* Force flag (0xFF) */
} rem_file_drop_hard_link_req_t;

void REM_FILE_$DROP_HARD_LINKU(void *addr_info, uid_t *dir_uid,
                                char *name, uint16_t name_len,
                                uint16_t flags2, status_$t *status)
{
    rem_file_drop_hard_link_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    int i;

    /* Initialize name field with spaces */
    for (i = 0; i < 32; i++) {
        request.name[i] = ' ';
    }

    /* Build request */
    request.magic = 0x80;
    request.opcode = 0x28;  /* DROP_HARD_LINK opcode */
    request.dir_uid = *dir_uid;

    /* Copy name (up to 32 bytes) */
    for (i = 0; i < 32 && i < name_len; i++) {
        request.name[i] = name[i];
    }

    request.name_len = name_len;
    request.flags2 = flags2;
    request.flags = 3;
    request.force_flag = 0xFF;

    /* Send request */
    REM_FILE_$SEND_REQUEST(addr_info, &request, 0x34,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);
}
