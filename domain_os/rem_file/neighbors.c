/*
 * rem_file/neighbors.c - REM_FILE_$NEIGHBORS (0x00E621B8, 164 bytes)
 *
 * Asks a remote file server whether two objects live on the same volume.
 *
 * Re-emitted against the listing for bead source-vaov: the reply length is
 * 0xBE (`move.w #0xbe,-(SP)` at 0x00E62224) into a buffer based at A6-0xC0,
 * and the answer is the byte at A6-0xB8, i.e. response+0x08 - not
 * response+0x04.
 *
 * Frame: `link.w A6,-0x178` + `movem.l {A5 A2}` (8 bytes), so the epilogue is
 * `movem.l (-0x180,A6)` (0x00E62252).
 */

#include "rem_file/rem_file_internal.h"

/*
 * The request, 0x18 bytes at A6-0x170 (`move.w #0x18,-(SP)` at 0x00E62232).
 * Offsets are the A6 displacement plus 0x170.
 */
typedef struct rem_file_$neighbors_req_t {
    uint16_t    msg_type;       /* 0x00: stamped by REM_FILE_$SEND_REQUEST */
    uint8_t     magic;          /* 0x02: 0x80        (0x00E621CA) */
    uint8_t     opcode;         /* 0x03: 0x10        (0x00E621D0) */
    uid_t       uid1;           /* 0x04:             (0x00E621DA) */
    uid_t       uid2;           /* 0x0C:             (0x00E621E6) */
    uint16_t    reserved;       /* 0x14: 3           (0x00E621EE) */
    int8_t      admin_flag;     /* 0x16: 0xFF when the caller is privileged */
    uint8_t     _pad_17;        /* 0x17 */
} rem_file_$neighbors_req_t;

_Static_assert(__builtin_offsetof(rem_file_$neighbors_req_t, uid1) == 0x04, "neighbors_req.uid1");
_Static_assert(__builtin_offsetof(rem_file_$neighbors_req_t, uid2) == 0x0C, "neighbors_req.uid2");
_Static_assert(__builtin_offsetof(rem_file_$neighbors_req_t, reserved) == 0x14, "neighbors_req.reserved");
_Static_assert(__builtin_offsetof(rem_file_$neighbors_req_t, admin_flag) == 0x16, "neighbors_req.admin_flag");
_Static_assert(sizeof(rem_file_$neighbors_req_t) == 0x18, "neighbors_req size");

/*
 * The reply.  Only one payload byte is read: `move.b (-0xb8,A6),D0b` at
 * 0x00E6224A, and A6-0xB8 is response+0x08.
 */
typedef struct rem_file_$neighbors_resp_t {
    uint16_t    pkt_flag;       /* 0x00 */
    uint8_t     magic;          /* 0x02 */
    uint8_t     opcode;         /* 0x03 */
    status_$t   status;         /* 0x04 */
    int8_t      are_neighbors;  /* 0x08 */
    uint8_t     rest[REM_FILE_RESPONSE_BUF_SIZE - 9];
} rem_file_$neighbors_resp_t;

_Static_assert(__builtin_offsetof(rem_file_$neighbors_resp_t, are_neighbors) == 0x08,
               "neighbors_resp.are_neighbors");

int8_t REM_FILE_$NEIGHBORS(void *location_info, uid_t *uid1, uid_t *uid2,
                           status_$t *status)
{
    rem_file_$neighbors_req_t  request;     /* A6-0x170 */
    rem_file_$neighbors_resp_t response;    /* A6-0xC0  */
    uint16_t received_len;                  /* A6-0x176 */
    uint16_t packet_id;                     /* A6-0x174 */
    /* A6-0x172: one cleared word serving as extra_data, bulk_data and
     * bulk_len (`clr.w (-0x172,A6)` at 0x00E6220C, then three `pea`s). */
    int16_t  nil_word = 0;

    request.magic  = REM_FILE_REQ_MAGIC;        /* 0x00E621CA */
    request.opcode = REM_FILE_OP_NEIGHBORS;     /* 0x00E621D0 */
    request.uid1   = *uid1;                     /* 0x00E621DA */
    request.uid2   = *uid2;                     /* 0x00E621E6 */
    request.reserved = 3;                       /* 0x00E621EE */
    /* 0x00E621F4-0x00E62208: `sgt` makes the Domain boolean 0xFF / 0x00. */
    request.admin_flag = REM_FILE_PROCESS_HAS_ADMIN() ? true : false;

    /* 0x00E62210-0x00E62242: thirteen arguments, 0x2C bytes popped. */
    REM_FILE_$SEND_REQUEST(location_info, &request, 0x18,
                           &nil_word, 0,
                           &response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &nil_word, 0,
                           &nil_word, &packet_id,
                           status);

    /* 0x00E62246-0x00E62250: `tst.l (A2)` is the whole status longword. */
    if (*status != status_$ok) {
        return 0;
    }
    return response.are_neighbors;
}
