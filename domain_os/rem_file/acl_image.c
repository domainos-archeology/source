/*
 * REM_FILE_$ACL_IMAGE - Get ACL image from remote file
 *
 * Retrieves the ACL (Access Control List) image from a remote file.  The
 * variable-length part of the ACL comes back as a bulk payload (up to 0x400
 * bytes) written straight into the caller's buffer; the reply header carries
 * the image length and an eleven-longword header.
 *
 * Original address: 0x00E627A8..0x00E6283A (148 bytes)
 * A5 = 0x00E823FC, the REM_FILE module base (lea at 0x00E627B0).
 *
 * Frame: "link.w A6,-0x178" plus a two-register movem, so the epilogue is
 * "movem.l (-0x180,A6)" (0x00E62832).  The request record base is A6-0x170
 * ("pea (-0x170,A6)" at 0x00E6280A) and the reply buffer base is A6-0xC0
 * ("pea (-0xc0,A6)" at 0x00E627FC).
 *
 * Arguments (A6 displacements):
 *   0x08 addr_info        long  (0x00E6280E)
 *   0x0C file_uid         long  (0x00E627C6)
 *   0x10 acl_type         byte  ("move.b (0x10,A6),D0b" at 0x00E627B6 - the
 *                                high byte of the word slot, i.e. a byte
 *                                parameter)
 *   0x12 acl_image_out    long  (0x00E627F0, the bulk destination)
 *   0x16 acl_len_out      long  (0x00E6281A)
 *   0x1A acl_header_out   long  (0x00E62826)
 *   0x1E status           long  (0x00E627E0)
 */

#include "rem_file/rem_file_internal.h"

/*
 * ACL image request record - 0x14 bytes ("move.w #0x14,-(SP)" at 0x00E62806).
 *
 * Stores, with the listing address and the A6 displacement used:
 *   +0x02 magic     0x00E627BA  (-0x16e)
 *   +0x03 opcode    0x00E627C0  (-0x16d)
 *   +0x04 file_uid  0x00E627CA/0x00E627CE (-0x16c, -0x168)
 *   +0x0C flags     0x00E627D6  (-0x164)
 *   +0x0E acl_type  0x00E627D2  (-0x162)
 * Bytes +0x0F..+0x13 are inside the declared length but are never stored.
 */
typedef struct rem_file_acl_image_req_t {
    uint16_t msg_type;      /* 0x00: set to 1 by SEND_REQUEST */
    uint8_t  magic;         /* 0x02: 0x80 */
    uint8_t  opcode;        /* 0x03: 0x64 = ACL image */
    uid_t    file_uid;      /* 0x04 */
    uint16_t flags;         /* 0x0C: literal 5 */
    uint8_t  acl_type;      /* 0x0E */
    uint8_t  unset[5];      /* 0x0F: within the 0x14 length, never stored */
} __attribute__((packed, aligned(2))) rem_file_acl_image_req_t;

_Static_assert(__builtin_offsetof(rem_file_acl_image_req_t, magic) == 0x02, "acl_image_req.magic");
_Static_assert(__builtin_offsetof(rem_file_acl_image_req_t, opcode) == 0x03, "acl_image_req.opcode");
_Static_assert(__builtin_offsetof(rem_file_acl_image_req_t, file_uid) == 0x04, "acl_image_req.file_uid");
_Static_assert(__builtin_offsetof(rem_file_acl_image_req_t, flags) == 0x0C, "acl_image_req.flags");
_Static_assert(__builtin_offsetof(rem_file_acl_image_req_t, acl_type) == 0x0E, "acl_image_req.acl_type");
_Static_assert(sizeof(rem_file_acl_image_req_t) == REM_FILE_ACL_IMAGE_REQ_LEN, "acl_image_req size");

/*
 * ACL image reply record.  Reply offsets are the A6 displacement plus 0xC0
 * (the buffer is at A6-0xC0).
 */
typedef struct rem_file_acl_image_resp_t {
    uint8_t  head[0x0A];     /* 0x00: pkt_flag/magic/opcode/status + 2 bytes */
    uint16_t acl_len;        /* 0x0A: "move.w (-0xb6,A6),(A0)"  0x00E6281E */
    uint32_t acl_header[11]; /* 0x0C: "lea (-0xb4,A6),A1" + 11 longs
                              *       ("moveq #0xa" / dbf)  0x00E62822 */
} __attribute__((packed, aligned(2))) rem_file_acl_image_resp_t;

_Static_assert(__builtin_offsetof(rem_file_acl_image_resp_t, acl_len) == 0x0A, "acl_image_resp.acl_len");
_Static_assert(__builtin_offsetof(rem_file_acl_image_resp_t, acl_header) == 0x0C, "acl_image_resp.acl_header");
_Static_assert(sizeof(rem_file_acl_image_resp_t) <= REM_FILE_RESPONSE_BUF_SIZE, "acl_image_resp fits");

/* The eleven longwords copied out at 0x00E6282A-0x00E6282E ("moveq #0xa"). */
#define REM_FILE_ACL_IMAGE_HEADER_LONGS 11

void REM_FILE_$ACL_IMAGE(void *addr_info, uid_t *file_uid,
                         uint8_t acl_type, void *acl_image_out,
                         uint16_t *acl_len_out, void *acl_header_out,
                         status_$t *status)
{
    rem_file_acl_image_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE] __attribute__((aligned(4)));
    uint16_t received_len;      /* A6-0x178, argument 8  (0x00E627F4) */
    int16_t  bulk_len;          /* A6-0x176, argument 11 (0x00E627E8) */
    uint16_t packet_id;         /* A6-0x174, argument 12 (0x00E627E4) */
    uint16_t zero = 0;          /* A6-0x172, cleared at 0x00E627DC */
    rem_file_acl_image_resp_t *resp;
    uint32_t *header_out;
    int i;

    /* Build request */
    request.magic = REM_FILE_REQ_MAGIC;
    request.opcode = REM_FILE_OP_ACL_IMAGE;  /* 0x64, 0x00E627C0 */
    request.file_uid = *file_uid;
    request.flags = 5;
    request.acl_type = acl_type;

    /*
     * Unlike the other builders this one hands the transport FOUR distinct
     * word cells: A6-0x178 (received_len), A6-0x176 (bulk_len), A6-0x174
     * (packet_id) and the zero word A6-0x172.  Only the zero word is cleared
     * (0x00E627DC); bulk_len goes in uninitialised and comes back holding the
     * payload byte count.  Aliasing bulk_len onto the zero word - as the tree
     * used to - would let the transport's bulk-length store overwrite the
     * request extra-length argument.
     */
    REM_FILE_$SEND_REQUEST(addr_info, &request, REM_FILE_ACL_IMAGE_REQ_LEN,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, acl_image_out, REM_FILE_ACL_IMAGE_BULK_MAX,
                           &bulk_len, &packet_id,
                           status);

    /* Extract ACL length and header from the reply */
    resp = (rem_file_acl_image_resp_t *)response;
    *acl_len_out = resp->acl_len;

    header_out = (uint32_t *)acl_header_out;
    for (i = 0; i < REM_FILE_ACL_IMAGE_HEADER_LONGS; i++) {
        header_out[i] = resp->acl_header[i];
    }
}
