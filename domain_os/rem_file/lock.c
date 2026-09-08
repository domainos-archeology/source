/*
 * rem_file/lock.c - REM_FILE_$LOCK (0x00E61AAE, 452 bytes)
 *
 * Locks an object on a remote node.  The caller's `extended` boolean picks
 * between two entirely different requests: the 0xA2-byte extended lock
 * (opcode 0x84), which carries the object-location record and the caller's
 * extended SID and gets a 144-byte lock record back, and the 0x22-byte plain
 * lock (opcode 0x0A), whose reply is two small fields.
 *
 * Re-emitted against the listing for bead source-1q0c.  Corrections: both
 * requests put the lock TYPE at +0x14 and the lock MODE at +0x16 (the tree
 * had them the other way round); the extended request's location record is at
 * +0x1C, its extended SID at +0x3C and its wait word at +0xA0; the plain
 * request's trailing word 1 is at +0x1E; and the reply fields are at +0x08,
 * +0x0C, +0x9C and +0xBC of the 0xBE-byte buffer based at A6-0xC0.
 *
 * Frame: `link.w A6,-0x178` + a nine-register movem (0x24 bytes), so the
 * epilogue is `movem.l (-0x19c,A6)` (0x00E61C68).
 *
 * Arguments: 0x08 location_block (A2), 0x0C lock_mode, 0x0E lock_type,
 * 0x10 flags, 0x12 wait_flag, 0x14 extended (a BYTE in a word slot),
 * 0x16 lock_key, 0x1A lock_key_out, 0x1E packet_id_out,
 * 0x22 lock_result (A3), 0x26 status (A4).
 */

#include "rem_file/rem_file_internal.h"

/*
 * The plain lock request, 0x22 bytes at A6-0x170
 * (`move.w #0x22,(-0x176,A6)` at 0x00E61BA8).
 */
typedef struct rem_file_$lock_req_t {
    uint16_t    msg_type;       /* 0x00: stamped by REM_FILE_$SEND_REQUEST */
    uint8_t     magic;          /* 0x02: 0x80  (0x00E61B58) */
    uint8_t     opcode;         /* 0x03: 0x0A  (0x00E61B5E) */
    uid_t       file_uid;       /* 0x04: location_block+0x08 (0x00E61B68) */
    uint32_t    lock_key;       /* 0x0C: D5 = (0x16,A6)      (0x00E61B70) */
    uint32_t    src_node;       /* 0x10: NODE_$ME            (0x00E61B74) */
    uint16_t    lock_type;      /* 0x14: D1 = (0xe,A6)       (0x00E61B80) */
    uint16_t    lock_mode;      /* 0x16: D0 = (0xc,A6)       (0x00E61B7C) */
    uint16_t    flags;          /* 0x18: 3                   (0x00E61B84) */
    int8_t      admin_flag;     /* 0x1A:                     (0x00E61B9E) */
    uint8_t     _pad_1b;        /* 0x1B */
    uint16_t    _pad_1c;        /* 0x1C: never written */
    uint16_t    reserved;       /* 0x1E: 1                   (0x00E61BA2) */
    uint16_t    _pad_20;        /* 0x20 */
} rem_file_$lock_req_t;

_Static_assert(__builtin_offsetof(rem_file_$lock_req_t, lock_key) == 0x0C, "lock_req.lock_key");
_Static_assert(__builtin_offsetof(rem_file_$lock_req_t, src_node) == 0x10, "lock_req.src_node");
_Static_assert(__builtin_offsetof(rem_file_$lock_req_t, lock_type) == 0x14, "lock_req.lock_type");
_Static_assert(__builtin_offsetof(rem_file_$lock_req_t, lock_mode) == 0x16, "lock_req.lock_mode");
_Static_assert(__builtin_offsetof(rem_file_$lock_req_t, flags) == 0x18, "lock_req.flags");
_Static_assert(__builtin_offsetof(rem_file_$lock_req_t, admin_flag) == 0x1A, "lock_req.admin_flag");
_Static_assert(__builtin_offsetof(rem_file_$lock_req_t, reserved) == 0x1E, "lock_req.reserved");
_Static_assert(sizeof(rem_file_$lock_req_t) == 0x22, "lock_req size");

/*
 * The extended lock request, 0xA2 bytes at A6-0x170
 * (`move.w #0xa2,(-0x176,A6)` at 0x00E61B50).
 */
typedef struct rem_file_$lock_ext_req_t {
    uint16_t    msg_type;       /* 0x00 */
    uint8_t     magic;          /* 0x02: 0x80  (0x00E61AE4) */
    uint8_t     opcode;         /* 0x03: 0x84  (0x00E61AEA) */
    uid_t       file_uid;       /* 0x04: location_block+0x08 (0x00E61AF4) */
    uint32_t    lock_key;       /* 0x0C: D5                  (0x00E61B0A) */
    uint32_t    src_node;       /* 0x10: NODE_$ME            (0x00E61B0E) */
    uint16_t    lock_type;      /* 0x14: D1                  (0x00E61B1A) */
    uint16_t    lock_mode;      /* 0x16: D0                  (0x00E61B16) */
    uint16_t    flags;          /* 0x18: D3, then bit 8 set when the caller
                                 *       is in a subsystem   (0x00E61B2C) */
    uint16_t    reserved;       /* 0x1A: 1                   (0x00E61B36) */
    uint32_t    location[8];    /* 0x1C: 8 longs from location_block+0x00
                                 *                           (0x00E61B04) */
    uint8_t     exsid[100];     /* 0x3C: ACL_$GET_EXSID      (0x00E61B3E) */
    uint16_t    wait_flag;      /* 0xA0: D4                  (0x00E61B32) */
} rem_file_$lock_ext_req_t;

_Static_assert(__builtin_offsetof(rem_file_$lock_ext_req_t, lock_key) == 0x0C, "lock_ext_req.lock_key");
_Static_assert(__builtin_offsetof(rem_file_$lock_ext_req_t, src_node) == 0x10, "lock_ext_req.src_node");
_Static_assert(__builtin_offsetof(rem_file_$lock_ext_req_t, lock_type) == 0x14, "lock_ext_req.lock_type");
_Static_assert(__builtin_offsetof(rem_file_$lock_ext_req_t, lock_mode) == 0x16, "lock_ext_req.lock_mode");
_Static_assert(__builtin_offsetof(rem_file_$lock_ext_req_t, flags) == 0x18, "lock_ext_req.flags");
_Static_assert(__builtin_offsetof(rem_file_$lock_ext_req_t, reserved) == 0x1A, "lock_ext_req.reserved");
_Static_assert(__builtin_offsetof(rem_file_$lock_ext_req_t, location) == 0x1C, "lock_ext_req.location");
_Static_assert(__builtin_offsetof(rem_file_$lock_ext_req_t, exsid) == 0x3C, "lock_ext_req.exsid");
_Static_assert(__builtin_offsetof(rem_file_$lock_ext_req_t, wait_flag) == 0xA0, "lock_ext_req.wait_flag");
_Static_assert(sizeof(rem_file_$lock_ext_req_t) == 0xA2, "lock_ext_req size");

/*
 * The reply.  Field offsets are the A6 displacement plus 0xC0.
 */
typedef struct rem_file_$lock_resp_t {
    uint16_t    pkt_flag;       /* 0x00 */
    uint8_t     magic;          /* 0x02 */
    uint8_t     opcode;         /* 0x03 */
    status_$t   status;         /* 0x04 */
    uint32_t    lock_handle;    /* 0x08: plain reply, "move.l (-0xb8,A6),
                                 *       (0x2c,A3)"          0x00E61C3A */
    uint16_t    lock_word;      /* 0x0C: plain reply's second field
                                 *       (0x00E61C52 / 0x00E61C62); also the
                                 *       first word of the extended reply's
                                 *       144-byte record (0x00E61BFE) */
    uint16_t    _f0e;           /* 0x0E */
    uint8_t     _f10[0x8C];     /* 0x10 */
    uint32_t    obj_loc[8];     /* 0x9C: copied back over the caller's
                                 *       location block (0x00E61C1A) */
    uint16_t    packet_id;      /* 0xBC: "move.w (-0x4,A6),(A1)"  0x00E61C34 */
} rem_file_$lock_resp_t;

_Static_assert(__builtin_offsetof(rem_file_$lock_resp_t, lock_handle) == 0x08, "lock_resp.lock_handle");
_Static_assert(__builtin_offsetof(rem_file_$lock_resp_t, lock_word) == 0x0C, "lock_resp.lock_word");
_Static_assert(__builtin_offsetof(rem_file_$lock_resp_t, obj_loc) == 0x9C, "lock_resp.obj_loc");
_Static_assert(__builtin_offsetof(rem_file_$lock_resp_t, packet_id) == 0xBC, "lock_resp.packet_id");
_Static_assert(sizeof(rem_file_$lock_resp_t) == 0xBE, "lock_resp size");

void REM_FILE_$LOCK(void *location_block, uint16_t lock_mode, uint16_t lock_type,
                    uint16_t flags, uint16_t wait_flag, int8_t extended,
                    uint32_t lock_key, uint16_t *lock_key_out,
                    uint16_t *packet_id_out, void *lock_result,
                    status_$t *status)
{
    /* One request area at A6-0x170; which record it holds depends on
     * `extended`.  0x174 bytes are reserved so both fit. */
    union {
        rem_file_$lock_req_t     plain;
        rem_file_$lock_ext_req_t ext;
        uint8_t                  raw[0x174];
    } request;
    rem_file_$lock_resp_t response;         /* A6-0xC0  */
    uint16_t request_len;                   /* A6-0x176 */
    uint16_t received_len;                  /* A6-0x178 */
    uint16_t packet_id;                     /* A6-0x172 */
    int16_t  nil_word = 0;                  /* A6-0x174 */
    uint32_t *loc_block = (uint32_t *)location_block;    /* A2 */
    uint32_t *result = (uint32_t *)lock_result;          /* A3 */
    int i;

    if (extended < 0) {
        /* --- the extended lock, 0x00E61AE4-0x00E61B56 ------------------- */
        request.ext.magic  = REM_FILE_REQ_MAGIC;
        request.ext.opcode = REM_FILE_OP_LOCK_EXT;

        request.ext.file_uid.high = loc_block[2];   /* 0x00E61AF4 */
        request.ext.file_uid.low  = loc_block[3];

        for (i = 0; i < 8; i++) {                   /* 0x00E61B04 */
            request.ext.location[i] = loc_block[i];
        }

        request.ext.lock_key  = lock_key;           /* 0x00E61B0A */
        request.ext.src_node  = NODE_$ME;           /* 0x00E61B0E */
        request.ext.lock_mode = lock_mode;          /* 0x00E61B16 */
        request.ext.lock_type = lock_type;          /* 0x00E61B1A */
        request.ext.flags     = flags;              /* 0x00E61B1E */

        /* 0x00E61B22-0x00E61B30.  `bset.b #0x0,(-0x158,A6)` sets bit 0 of the
         * byte at +0x18, which is the HIGH byte of that word - bit 8. */
        if ((int8_t)ACL_$IN_SUBSYS() < 0) {   /* `tst.b D0b / bpl` at 0x00E61B28 */
            request.ext.flags |= 0x0100;
        }

        request.ext.wait_flag = wait_flag;          /* 0x00E61B32 */
        request.ext.reserved  = 1;                  /* 0x00E61B36 */

        ACL_$GET_EXSID(request.ext.exsid, status);  /* 0x00E61B42 */
        if (*status != status_$ok) {                /* 0x00E61B4A */
            return;
        }

        request_len = 0xA2;                         /* 0x00E61B50 */
    } else {
        /* --- the plain lock, 0x00E61B58-0x00E61BAC ---------------------- */
        request.plain.magic  = REM_FILE_REQ_MAGIC;
        request.plain.opcode = REM_FILE_OP_LOCK;

        request.plain.file_uid.high = loc_block[2]; /* 0x00E61B68 */
        request.plain.file_uid.low  = loc_block[3];

        request.plain.lock_key  = lock_key;         /* 0x00E61B70 */
        request.plain.src_node  = NODE_$ME;         /* 0x00E61B74 */
        request.plain.lock_mode = lock_mode;        /* 0x00E61B7C */
        request.plain.lock_type = lock_type;        /* 0x00E61B80 */
        request.plain.flags     = 3;                /* 0x00E61B84 */
        request.plain.admin_flag = REM_FILE_PROCESS_HAS_ADMIN() ? true : false;
        request.plain.reserved  = 1;                /* 0x00E61BA2 */

        request_len = 0x22;                         /* 0x00E61BA8 */
    }

    /* 0x00E61BAE-0x00E61BE4.  The address record is location_block+0x10. */
    REM_FILE_$SEND_REQUEST((uint8_t *)location_block + 0x10,
                           request.raw, (int16_t)request_len,
                           &nil_word, 0,
                           &response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &nil_word, 0,
                           &nil_word, &packet_id,
                           status);

    /* 0x00E61BE8-0x00E61BF4 */
    *packet_id_out = packet_id;

    if (*status != status_$ok) {                    /* 0x00E61BF6 */
        return;
    }

    if (extended < 0) {
        /* 0x00E61BFE-0x00E61C0C: 36 longwords from response+0x0C. */
        const uint32_t *src = (const uint32_t *)&response.lock_word;
        for (i = 0; i < 36; i++) {
            result[i] = src[i];
        }

        /* 0x00E61C0E-0x00E61C18: the caller's own address record is written
         * INTO the reply buffer at response+0xAC / +0xB0 first, so the copy
         * that follows hands those two longwords straight back. */
        {
            uint32_t *resp_l = (uint32_t *)&response;
            resp_l[0xAC / 4] = loc_block[4];
            resp_l[0xB0 / 4] = loc_block[5];
        }

        /* 0x00E61C1A-0x00E61C28: 8 longwords from response+0x9C. */
        for (i = 0; i < 8; i++) {
            loc_block[i] = response.obj_loc[i];
        }

        /* 0x00E61C2A */
        ((uint8_t *)location_block)[0x1D] |= 0x80;

        /* 0x00E61C30-0x00E61C36 */
        *lock_key_out = response.packet_id;
    } else {
        /* 0x00E61C3A */
        result[0x2C / 4] = response.lock_handle;

        /* 0x00E61C40-0x00E61C66.  D1 is the received reply length. */
        if (received_len == 0x12) {
            ((uint16_t *)result)[0x30 / 2] = 0;
        } else if (received_len == 0x0E) {
            /* `lsl.w #0x8` then `andi.w #-0x800` (0xF800). */
            ((uint16_t *)result)[0x30 / 2] =
                (uint16_t)((uint16_t)(response.lock_word << 8) & 0xF800u);
        } else {
            ((uint16_t *)result)[0x30 / 2] = response.lock_word;
        }
    }
}
