/*
 * rem_file/create_area.c - REM_FILE_$CREATE_AREA (0x00E62622, 170 bytes)
 *
 * Asks a remote file server to create an area and returns its handle, plus
 * the packet size the network layer will actually use for it.
 *
 * Re-emitted against the listing for bead source-hzfi: the request holds
 * area_offset (the caller's fourth argument, D2) at +0x10 and area_size (the
 * third, D1) at +0x18 - the tree had them the other way round - and the reply
 * fields sit at +0x08 and +0x0A of the 0xBE-byte buffer based at A6-0xC0.
 *
 * Frame: `link.w A6,-0x17c` + `movem.l {A5 A2 D2}` (0x0C bytes), so the
 * epilogue is `movem.l (-0x188,A6)` (0x00E626C2).
 */

#include "rem_file/rem_file_internal.h"

/*
 * The request, 0x1C bytes at A6-0x170 (`move.w #0x1c,-(SP)` at 0x00E62680).
 * Nothing is written between +0x04 and +0x0C, or at +0x14.
 */
typedef struct rem_file_$create_area_req_t {
    uint16_t    msg_type;       /* 0x00: stamped by REM_FILE_$SEND_REQUEST */
    uint8_t     magic;          /* 0x02: 0x80  (0x00E62640) */
    uint8_t     opcode;         /* 0x03: 0x86  (0x00E62646) */
    uint8_t     _pad_04[8];     /* 0x04 */
    uint32_t    area_type;      /* 0x0C: D0 = (0xc,A6)   (0x00E6264C) */
    uint32_t    area_offset;    /* 0x10: D2 = (0x14,A6)  (0x00E62654) */
    uint32_t    _pad_14;        /* 0x14 */
    uint32_t    area_size;      /* 0x18: D1 = (0x10,A6)  (0x00E62650) */
} rem_file_$create_area_req_t;

_Static_assert(__builtin_offsetof(rem_file_$create_area_req_t, area_type) == 0x0C, "create_area_req.area_type");
_Static_assert(__builtin_offsetof(rem_file_$create_area_req_t, area_offset) == 0x10, "create_area_req.area_offset");
_Static_assert(__builtin_offsetof(rem_file_$create_area_req_t, area_size) == 0x18, "create_area_req.area_size");
_Static_assert(sizeof(rem_file_$create_area_req_t) == 0x1C, "create_area_req size");

/*
 * The reply.  `move.w (-0xb8,A6),D0w` at 0x00E626BE is response+0x08 and
 * `move.w (-0xb6,A6),...` at 0x00E626A4 is response+0x0A.
 */
typedef struct rem_file_$create_area_resp_t {
    uint16_t    pkt_flag;       /* 0x00 */
    uint8_t     magic;          /* 0x02 */
    uint8_t     opcode;         /* 0x03 */
    status_$t   status;         /* 0x04 */
    uint16_t    area_handle;    /* 0x08 */
    uint16_t    pkt_size;       /* 0x0A */
    uint8_t     rest[REM_FILE_RESPONSE_BUF_SIZE - 0x0C];
} rem_file_$create_area_resp_t;

_Static_assert(__builtin_offsetof(rem_file_$create_area_resp_t, area_handle) == 0x08, "create_area_resp.area_handle");
_Static_assert(__builtin_offsetof(rem_file_$create_area_resp_t, pkt_size) == 0x0A, "create_area_resp.pkt_size");

/* Shortest reply that carries a packet size (0x00E62696 `cmpi.w #0xa,D0w`). */
#define CREATE_AREA_REPLY_WITH_PKT_SIZE 0x0A

uint16_t REM_FILE_$CREATE_AREA(void *addr_info, uint32_t area_type,
                               uint32_t area_size, uint32_t area_offset,
                               uint8_t flags, uint16_t *pkt_size_out,
                               status_$t *status)
{
    rem_file_$create_area_req_t  request;   /* A6-0x170 */
    rem_file_$create_area_resp_t response;  /* A6-0xC0  */
    uint16_t received_len;                  /* A6-0x178 */
    uint16_t packet_id;                     /* A6-0x176 */
    int16_t  nil_word = 0;                  /* A6-0x174 */
    uint16_t pkt_size;                      /* A6-0x172 */

    /* The word at (0x18,A6) - the caller's `flags` - is never read by the
     * original; it is accepted and discarded. */
    (void)flags;

    request.magic       = REM_FILE_REQ_MAGIC;       /* 0x00E62640 */
    request.opcode      = REM_FILE_OP_CREATE_AREA;  /* 0x00E62646 */
    request.area_type   = area_type;                /* 0x00E6264C */
    request.area_size   = area_size;                /* 0x00E62650 */
    request.area_offset = area_offset;              /* 0x00E62654 */

    /* 0x00E6265C-0x00E6268E */
    REM_FILE_$SEND_REQUEST(addr_info, &request, 0x1C,
                           &nil_word, 0,
                           &response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &nil_word, 0,
                           &nil_word, &packet_id,
                           status);

    /* 0x00E62692-0x00E626A8: `cmpi.w #0xa,D0w / bgt` - a reply of 0x0A bytes
     * or less carries no packet size, so 1KB is assumed. */
    if ((int16_t)received_len > CREATE_AREA_REPLY_WITH_PKT_SIZE) {
        pkt_size = response.pkt_size;
    } else {
        pkt_size = 0x400;
    }

    /* 0x00E626AA-0x00E626BC: a word result through a Pascal result slot. */
    *pkt_size_out = NETWORK_$GET_PKT_SIZE(addr_info, pkt_size);

    return response.area_handle;                    /* 0x00E626BE */
}
