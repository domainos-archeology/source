/*
 * REM_FILE_$FILE_SET_PROT - Set file protection on remote file server
 *
 * Sets the protection attributes for a file on a remote server.
 * Returns the modification time if successful.
 *
 * Original address: 0x00E62B64
 * Size: 190 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * File set protection request structure
 */
typedef struct {
    uint16_t msg_type;          /* Set to 1 by SEND_REQUEST */
    uint8_t magic;              /* 0x80 */
    uint8_t opcode;             /* 0x80 = File set prot */
    uid_t file_uid;             /* 0x04: (0xc,A6)             0x00E62B9E */
    uint8_t flag;               /* 0x0C: D0b = the BYTE at (0x1a,A6)
                                 *             0x00E62BBA */
    uint8_t _pad_0d;            /* 0x0D */
    uint16_t flags;             /* 0x0E: D1 = (0x14,A6)       0x00E62BB6 */
    uint32_t prot_data1[13];    /* 0x10: 13 longs from (0x10,A6)  0x00E62BAA */
    uint32_t prot_data2[25];    /* 0x44: 25 longs from (0x16,A6)  0x00E62B8E */
} rem_file_file_set_prot_req_t;

/* Request offsets are the A6 displacement plus 0x170. */
_Static_assert(__builtin_offsetof(rem_file_file_set_prot_req_t, flag) == 0x0C, "set_prot_req.flag");
_Static_assert(__builtin_offsetof(rem_file_file_set_prot_req_t, flags) == 0x0E, "set_prot_req.flags");
_Static_assert(__builtin_offsetof(rem_file_file_set_prot_req_t, prot_data1) == 0x10, "set_prot_req.prot_data1");
_Static_assert(__builtin_offsetof(rem_file_file_set_prot_req_t, prot_data2) == 0x44, "set_prot_req.prot_data2");
_Static_assert(sizeof(rem_file_file_set_prot_req_t) == 0xA8, "set_prot_req size");

/*
 * File set protection response structure
 */
typedef struct {
    uint8_t  head[0x40];        /* 0x00 */
    uint32_t mtime_high;        /* 0x40: "move.l (-0x80,A6),(A2)"  0x00E62C04 */
    uint16_t mtime_low;         /* 0x44: "move.w (-0x7c,A6),(0x4,A2)"  0x00E62C08 */
    uint8_t  rest[REM_FILE_RESPONSE_BUF_SIZE - 0x46];
} rem_file_file_set_prot_resp_t;

/* Reply offsets are the A6 displacement plus 0xC0 (buffer at A6-0xC0). */
_Static_assert(__builtin_offsetof(rem_file_file_set_prot_resp_t, mtime_high) == 0x40, "file_set_prot_resp.mtime_high");
_Static_assert(__builtin_offsetof(rem_file_file_set_prot_resp_t, mtime_low) == 0x44, "file_set_prot_resp.mtime_low");

void REM_FILE_$FILE_SET_PROT(void *addr_info, uid_t *file_uid,
                              void *prot_data1, uint16_t flags,
                              void *prot_data2, uint8_t flag,
                              clock_t *mtime_out, status_$t *status)
{
    rem_file_file_set_prot_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    int i;
    uint32_t *data1 = (uint32_t *)prot_data1;
    uint32_t *data2 = (uint32_t *)prot_data2;

    /* Build request */
    request.magic = 0x80;
    request.opcode = REM_FILE_OP_FILE_SET_PROT;  /* 0x80, 0x00E62B84 */
    request.file_uid = *file_uid;

    /* Copy protection data block 1 (13 uint32s) */
    for (i = 0; i < 13; i++) {
        request.prot_data1[i] = data1[i];
    }

    /* Copy protection data block 2 (25 uint32s) */
    for (i = 0; i < 25; i++) {
        request.prot_data2[i] = data2[i];
    }

    request.flags = flags;
    request.flag = flag;

    /* Send request */
    REM_FILE_$SEND_REQUEST(addr_info, &request, 0xA8,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    /* Extract modification time from response or get current time */
    if (received_len == REM_FILE_RESPONSE_BUF_SIZE) {
        rem_file_file_set_prot_resp_t *resp = (rem_file_file_set_prot_resp_t *)response;
        mtime_out->high = resp->mtime_high;
        mtime_out->low = resp->mtime_low;
    } else {
        TIME_$CLOCK(mtime_out);
    }
}
