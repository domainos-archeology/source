/*
 * REM_FILE_$TEST - Test connectivity to remote file server
 *
 * Sends a simple test request to a remote file server to verify
 * network connectivity. Uses a nil UID and opcode 0.
 *
 * Original address: 0x00E62374
 * Size: 100 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * External data references
 */
extern uid_t UID_$NIL;  /* Nil UID */

/*
 * Test request structure
 */
typedef struct {
    uint16_t msg_type;      /* Set to 1 by SEND_REQUEST */
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x00 = test */
    uid_t nil_uid;          /* Nil UID (8 bytes) */
} rem_file_test_req_t;

void REM_FILE_$TEST(void *addr_info, status_$t *status)
{
    rem_file_test_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;

    /* Build request */
    request.magic = 0x80;
    request.opcode = 0x00;  /* TEST opcode */
    request.nil_uid = UID_$NIL;

    /* Send request */
    REM_FILE_$SEND_REQUEST(addr_info, &request, 0x12,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);
}
