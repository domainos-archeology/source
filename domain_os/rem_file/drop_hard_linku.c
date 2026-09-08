/*
 * rem_file/drop_hard_linku.c - REM_FILE_$DROP_HARD_LINKU (0x00E62588, 154 bytes)
 *
 * Removes a directory entry on a remote node.
 *
 * Re-emitted against the listing for bead source-mfx0: like its sibling
 * REM_FILE_$NAME_ADD_HARD_LINKU, only one longword of spaces is written
 * (`move.l #0x20202020,(-0x164,A6)` at 0x00E6259E) and the copy that follows
 * moves exactly 32 bytes (`moveq #0x1f,D2` / `dbf`) without consulting
 * name_len.
 *
 * Frame: `link.w A6,-0x178` + `movem.l {A5 D2}` (8 bytes), so the epilogue is
 * `movem.l (-0x180,A6)` (0x00E62618).
 */

#include "rem_file/rem_file_internal.h"

/*
 * The request, 0x34 bytes at A6-0x170 (`move.w #0x34,-(SP)` at 0x00E62608).
 */
typedef struct rem_file_$drop_hard_link_req_t {
    uint16_t    msg_type;       /* 0x00: stamped by REM_FILE_$SEND_REQUEST */
    uint8_t     magic;          /* 0x02: 0x80  (0x00E625A6) */
    uint8_t     opcode;         /* 0x03: 0x28  (0x00E625AC) */
    uid_t       dir_uid;        /* 0x04:       (0x00E625B6) */
    char        name[32];       /* 0x0C: 32 bytes copied unconditionally
                                 *       (0x00E625C8) */
    uint16_t    name_len;       /* 0x2C: D1 = (0x14,A6)  (0x00E625CE) */
    uint16_t    flags;          /* 0x2E: D0 = (0x16,A6)  (0x00E625D2) */
    uint16_t    reserved;       /* 0x30: 3               (0x00E625D6) */
    int8_t      force_flag;     /* 0x32: `st`            (0x00E625DC) */
    uint8_t     _pad_33;        /* 0x33 */
} rem_file_$drop_hard_link_req_t;

_Static_assert(__builtin_offsetof(rem_file_$drop_hard_link_req_t, dir_uid) == 0x04, "drop_hard_link_req.dir_uid");
_Static_assert(__builtin_offsetof(rem_file_$drop_hard_link_req_t, name) == 0x0C, "drop_hard_link_req.name");
_Static_assert(__builtin_offsetof(rem_file_$drop_hard_link_req_t, name_len) == 0x2C, "drop_hard_link_req.name_len");
_Static_assert(__builtin_offsetof(rem_file_$drop_hard_link_req_t, flags) == 0x2E, "drop_hard_link_req.flags");
_Static_assert(__builtin_offsetof(rem_file_$drop_hard_link_req_t, reserved) == 0x30, "drop_hard_link_req.reserved");
_Static_assert(__builtin_offsetof(rem_file_$drop_hard_link_req_t, force_flag) == 0x32, "drop_hard_link_req.force_flag");
_Static_assert(sizeof(rem_file_$drop_hard_link_req_t) == 0x34, "drop_hard_link_req size");

void REM_FILE_$DROP_HARD_LINKU(void *addr_info, uid_t *dir_uid,
                               char *name, uint16_t name_len,
                               uint16_t flags, status_$t *status)
{
    rem_file_$drop_hard_link_req_t request;             /* A6-0x170 */
    uint8_t  response[REM_FILE_RESPONSE_BUF_SIZE];      /* A6-0xC0  */
    uint16_t received_len;                              /* A6-0x176 */
    uint16_t packet_id;                                 /* A6-0x174 */
    int16_t  nil_word = 0;                              /* A6-0x172 */
    int      i;

    /* 0x00E6259E: one longword of spaces only. */
    request.name[0] = ' ';
    request.name[1] = ' ';
    request.name[2] = ' ';
    request.name[3] = ' ';

    request.magic   = REM_FILE_REQ_MAGIC;                   /* 0x00E625A6 */
    request.opcode  = REM_FILE_OP_DROP_HARD_LINKU;          /* 0x00E625AC */
    request.dir_uid = *dir_uid;                             /* 0x00E625B6 */

    /* 0x00E625C6-0x00E625CA: 32 bytes, not clipped to name_len. */
    for (i = 0; i < 32; i++) {
        request.name[i] = name[i];
    }

    request.name_len = name_len;                            /* 0x00E625CE */
    request.flags    = flags;                               /* 0x00E625D2 */
    request.reserved = 3;                                   /* 0x00E625D6 */
    request.force_flag = true;                              /* 0x00E625DC */

    /* 0x00E625E0-0x00E62614 */
    REM_FILE_$SEND_REQUEST(addr_info, &request, 0x34,
                           &nil_word, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &nil_word, 0,
                           &nil_word, &packet_id,
                           status);
}
