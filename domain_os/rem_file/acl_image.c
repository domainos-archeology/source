/*
 * REM_FILE_$ACL_IMAGE - Get ACL image from remote file
 *
 * Retrieves the ACL (Access Control List) image from a remote file.
 * Returns the ACL data which can be quite large (up to 1KB bulk data).
 *
 * Original address: 0x00E627A8
 * Size: 148 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * ACL image request structure
 */
typedef struct {
    uint16_t msg_type;      /* Set to 1 by SEND_REQUEST */
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x64 = ACL image */
    uid_t file_uid;         /* File UID (8 bytes) */
    uint16_t flags;         /* Flags (value 5) */
    uint8_t acl_type;       /* ACL type */
} rem_file_acl_image_req_t;

/*
 * ACL image response structure
 */
typedef struct {
    uint8_t  head[0x0A];     /* 0x00: pkt_flag/magic/opcode/status + 2 bytes */
    uint16_t acl_len;        /* 0x0A: "move.w (-0xb6,A6),(A0)"  0x00E6281E */
    uint32_t acl_header[11]; /* 0x0C: "lea (-0xb4,A6),A1" + 11 longs
                              *       (dbf #0xA)           0x00E62822 */
} rem_file_acl_image_resp_t;

/* Reply offsets are the A6 displacement plus 0xC0 (the buffer is at A6-0xC0). */
_Static_assert(__builtin_offsetof(rem_file_acl_image_resp_t, acl_len) == 0x0A, "acl_image_resp.acl_len");
_Static_assert(__builtin_offsetof(rem_file_acl_image_resp_t, acl_header) == 0x0C, "acl_image_resp.acl_header");

void REM_FILE_$ACL_IMAGE(void *addr_info, uid_t *file_uid,
                         uint8_t acl_type, void *bulk_data_out,
                         uint16_t *acl_len_out, void *acl_header_out,
                         status_$t *status)
{
    rem_file_acl_image_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    int i;

    /* Build request */
    request.magic = 0x80;
    request.opcode = REM_FILE_OP_ACL_IMAGE;  /* 0x64, 0x00E627C0 */
    request.file_uid = *file_uid;
    request.flags = 5;
    request.acl_type = acl_type;

    /* Send request - note this uses bulk data transfer (0x400 max) */
    REM_FILE_$SEND_REQUEST(addr_info, &request, 0x14,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, bulk_data_out, 0x400,
                           (int16_t *)&zero, &packet_id,
                           status);

    /* Extract ACL length and header from response */
    rem_file_acl_image_resp_t *resp = (rem_file_acl_image_resp_t *)response;
    *acl_len_out = resp->acl_len;

    /* Copy ACL header (11 uint32s) */
    uint32_t *header_out = (uint32_t *)acl_header_out;
    for (i = 0; i < 11; i++) {
        header_out[i] = resp->acl_header[i];
    }
}
