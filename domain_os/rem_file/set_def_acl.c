/*
 * REM_FILE_$SET_DEF_ACL - Set default ACL on remote directory
 *
 * Sets the default ACL for a directory on a remote file server.
 * New files created in the directory will inherit this ACL.
 *
 * Original address: 0x00E622EE..0x00E62372 (134 bytes)
 * A5 = 0x00E823FC, the REM_FILE module base (lea at 0x00E622F4).
 *
 * Frame: "link.w A6,-0x178" plus "pea (A5)", so the epilogue is
 * "movea.l (-0x17c,A6),A5" (0x00E6236C).  The request record base is A6-0x170
 * ("pea (-0x170,A6)" at 0x00E62360).
 *
 * Arguments (A6 displacements):
 *   0x08 vol_uid    long  (0x00E62364)
 *   0x0C dir_uid    long  (0x00E62306)
 *   0x10 acl_uid    long  (0x00E62312)
 *   0x14 owner_uid  long  (0x00E6231E)
 *   0x18 status     long  (0x00E62338)
 */

#include "rem_file/rem_file_internal.h"

/*
 * Set default ACL request record - 0x20 bytes ("move.w #0x20,-(SP)" at
 * 0x00E6235C).
 *
 * Stores, with the listing address and the A6 displacement used:
 *   +0x02 magic       0x00E622FA  (-0x16e)
 *   +0x03 opcode      0x00E62300  (-0x16d)
 *   +0x04 dir_uid     0x00E6230A/0x00E6230E (-0x16c, -0x168)
 *   +0x0C acl_uid     0x00E62316/0x00E6231A (-0x164, -0x160)
 *   +0x14 owner_uid   0x00E62322/0x00E62326 (-0x15c, -0x158)
 *   +0x1C flags       0x00E6232A  (-0x154)
 *   +0x1E force_flag  0x00E62330  ("st (-0x152,A6)", so 0xFF = Pascal true)
 */
typedef struct rem_file_set_def_acl_req_t {
    uint16_t msg_type;      /* 0x00: set to 1 by SEND_REQUEST */
    uint8_t  magic;         /* 0x02: 0x80 */
    uint8_t  opcode;        /* 0x03: 0x18 = set default ACL */
    uid_t    dir_uid;       /* 0x04 */
    uid_t    acl_uid;       /* 0x0C */
    uid_t    owner_uid;     /* 0x14 */
    uint16_t flags;         /* 0x1C: literal 3 */
    boolean  force_flag;    /* 0x1E: "st", i.e. true */
    uint8_t  pad_1f;        /* 0x1F: never stored, sent as-is */
} __attribute__((packed, aligned(2))) rem_file_set_def_acl_req_t;

_Static_assert(__builtin_offsetof(rem_file_set_def_acl_req_t, magic) == 0x02, "set_def_acl_req.magic");
_Static_assert(__builtin_offsetof(rem_file_set_def_acl_req_t, opcode) == 0x03, "set_def_acl_req.opcode");
_Static_assert(__builtin_offsetof(rem_file_set_def_acl_req_t, dir_uid) == 0x04, "set_def_acl_req.dir_uid");
_Static_assert(__builtin_offsetof(rem_file_set_def_acl_req_t, acl_uid) == 0x0C, "set_def_acl_req.acl_uid");
_Static_assert(__builtin_offsetof(rem_file_set_def_acl_req_t, owner_uid) == 0x14, "set_def_acl_req.owner_uid");
_Static_assert(__builtin_offsetof(rem_file_set_def_acl_req_t, flags) == 0x1C, "set_def_acl_req.flags");
_Static_assert(__builtin_offsetof(rem_file_set_def_acl_req_t, force_flag) == 0x1E, "set_def_acl_req.force_flag");
_Static_assert(sizeof(rem_file_set_def_acl_req_t) == REM_FILE_SET_DEF_ACL_REQ_LEN, "set_def_acl_req size");

void REM_FILE_$SET_DEF_ACL(void *vol_uid, uid_t *dir_uid, uid_t *acl_uid,
                           uid_t *owner_uid, status_$t *status)
{
    rem_file_set_def_acl_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;      /* A6-0x176, argument 8  (0x00E6234A) */
    uint16_t packet_id;         /* A6-0x174, argument 12 (0x00E6233C) */
    uint16_t zero = 0;          /* A6-0x172, cleared at 0x00E62334 */

    /* Build request */
    request.magic = REM_FILE_REQ_MAGIC;
    request.opcode = REM_FILE_OP_SET_DEF_ACL;  /* 0x18, 0x00E62300 */
    request.dir_uid = *dir_uid;
    request.acl_uid = *acl_uid;
    request.owner_uid = *owner_uid;
    request.flags = 3;
    request.force_flag = true;

    /* Arguments 4, 9 and 11 all point at the single zero word A6-0x172. */
    REM_FILE_$SEND_REQUEST(vol_uid, &request, REM_FILE_SET_DEF_ACL_REQ_LEN,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);
}
