/*
 * REM_FILE_$UNLOCK_ALL - Release all remote file locks
 *
 * Broadcasts an unlock-all message to all remote nodes to release
 * any file locks held by this node. Called during initialization
 * and cleanup to ensure no stale locks remain.
 *
 * Original address: 0x00E61C72
 * Size: 164 bytes
 */

#include "rem_file/rem_file_internal.h"
#include "pkt/pkt.h"

/*
 * External data references
 */
extern uint8_t DAT_00e2e380[];  /* PKT info template */
extern uid_t UID_$NIL;          /* Nil UID */

/*
 * Unlock all request structure
 */
typedef struct {
    uint16_t reserved;      /* Reserved (value 1) */
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x12 = unlock all */
    uid_t nil_uid;          /* Nil UID (8 bytes) */
    uint16_t flags;         /* Flags (value 3) */
    int8_t admin_flag;      /* Process admin flag */
} rem_file_unlock_all_req_t;

void REM_FILE_$UNLOCK_ALL(void)
{
    rem_file_unlock_all_req_t request;
    uint8_t pkt_info[30];  /* Packet info structure */
    uint16_t len_out;
    uint16_t extra;
    status_$t status;
    int i;

    /* Build request */
    request.reserved = 1;
    request.magic = 0x80;
    request.opcode = 0x12;  /* UNLOCK_ALL opcode */
    request.nil_uid = UID_$NIL;
    request.flags = 3;
    request.admin_flag = REM_FILE_PROCESS_HAS_ADMIN() ? -1 : 0;

    /* Copy packet info template and set flag */
    for (i = 0; i < 7; i++) {
        ((uint32_t *)pkt_info)[i] = ((uint32_t *)DAT_00e2e380)[i];
    }
    ((uint16_t *)pkt_info)[14] = ((uint16_t *)DAT_00e2e380)[14];
    pkt_info[1] |= 0x80;

    /* Send broadcast packet to all nodes
     * Dest node = 0 (broadcast)
     * Dest socket = 2 (file server)
     * Src socket = 9999 (temporary socket for this operation)
     */
    PKT_$SEND_INTERNET(0, 0, 2, 0, NODE_$ME, 9999,
                       pkt_info, 0,
                       &request, 0x10,
                       NULL, 0,  /* No extra data */
                       &len_out, &extra, &status);
}
