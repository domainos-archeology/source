/*
 * rem_file/name_add_hard_linku.c - REM_FILE_$NAME_ADD_HARD_LINKU
 * (0x00E624EC, 156 bytes)
 *
 * Adds a directory entry on a remote node that points at an existing object.
 *
 * Re-emitted against the listing for bead source-mfx0.  Two corrections:
 * only the FIRST FOUR bytes of the name field are pre-set to spaces
 * (`move.l #0x20202020,(-0x164,A6)` at 0x00E624FC - one longword, not 32
 * bytes), and the copy that follows moves exactly 32 bytes
 * (`moveq #0x1f,D1` / `dbf`) regardless of the caller's name_len; name_len is
 * only stored into the request.  The file UID also lands at 0x2E, which is
 * only 2-aligned, so the record is packed.
 *
 * Frame: `link.w A6,-0x178` + `pea (A5)` (4 bytes), so the epilogue is
 * `movea.l (-0x17c,A6),A5` (0x00E62580).
 */

#include "rem_file/rem_file_internal.h"

/*
 * The request, 0x3A bytes at A6-0x170 (`move.w #0x3a,-(SP)` at 0x00E62570).
 */
typedef struct rem_file_$add_hard_link_req_t {
    uint16_t    msg_type;       /* 0x00: stamped by REM_FILE_$SEND_REQUEST */
    uint8_t     magic;          /* 0x02: 0x80  (0x00E62504) */
    uint8_t     opcode;         /* 0x03: 0x22  (0x00E6250A) */
    uid_t       dir_uid;        /* 0x04:       (0x00E62514) */
    char        name[32];       /* 0x0C: 32 bytes copied unconditionally
                                 *       (0x00E62528) */
    uint16_t    name_len;       /* 0x2C:       (0x00E6252E) */
    uid_t       file_uid;       /* 0x2E:       (0x00E62536) */
    uint16_t    reserved;       /* 0x36: 3     (0x00E6253E) */
    int8_t      force_flag;     /* 0x38: `st`  (0x00E62544) */
    uint8_t     _pad_39;        /* 0x39 */
} __attribute__((packed)) rem_file_$add_hard_link_req_t;

_Static_assert(__builtin_offsetof(rem_file_$add_hard_link_req_t, dir_uid) == 0x04, "add_hard_link_req.dir_uid");
_Static_assert(__builtin_offsetof(rem_file_$add_hard_link_req_t, name) == 0x0C, "add_hard_link_req.name");
_Static_assert(__builtin_offsetof(rem_file_$add_hard_link_req_t, name_len) == 0x2C, "add_hard_link_req.name_len");
_Static_assert(__builtin_offsetof(rem_file_$add_hard_link_req_t, file_uid) == 0x2E, "add_hard_link_req.file_uid");
_Static_assert(__builtin_offsetof(rem_file_$add_hard_link_req_t, reserved) == 0x36, "add_hard_link_req.reserved");
_Static_assert(__builtin_offsetof(rem_file_$add_hard_link_req_t, force_flag) == 0x38, "add_hard_link_req.force_flag");
_Static_assert(sizeof(rem_file_$add_hard_link_req_t) == 0x3A, "add_hard_link_req size");

void REM_FILE_$NAME_ADD_HARD_LINKU(void *addr_info, uid_t *dir_uid,
                                   char *name, uint16_t name_len,
                                   uid_t *file_uid, status_$t *status)
{
    rem_file_$add_hard_link_req_t request;              /* A6-0x170 */
    uint8_t  response[REM_FILE_RESPONSE_BUF_SIZE];      /* A6-0xC0  */
    uint16_t received_len;                              /* A6-0x176 */
    uint16_t packet_id;                                 /* A6-0x174 */
    int16_t  nil_word = 0;                              /* A6-0x172 */
    int      i;

    /* 0x00E624FC: one longword of spaces at the head of the name field.  The
     * remaining 28 bytes are whatever the frame held - and are then
     * overwritten wholesale by the copy below, so the store only matters if
     * the caller's name buffer is shorter than four bytes and faults. */
    request.name[0] = ' ';
    request.name[1] = ' ';
    request.name[2] = ' ';
    request.name[3] = ' ';

    request.magic   = REM_FILE_REQ_MAGIC;                   /* 0x00E62504 */
    request.opcode  = REM_FILE_OP_NAME_ADD_HARD_LINKU;      /* 0x00E6250A */
    request.dir_uid = *dir_uid;                             /* 0x00E62514 */

    /* 0x00E62524-0x00E6252A: `moveq #0x1f,D1` + `dbf` copies 32 bytes, NOT
     * clipped to name_len. */
    for (i = 0; i < 32; i++) {
        request.name[i] = name[i];
    }

    request.name_len = name_len;                            /* 0x00E6252E */
    request.file_uid = *file_uid;                           /* 0x00E62536 */
    request.reserved = 3;                                   /* 0x00E6253E */
    request.force_flag = true;                              /* 0x00E62544 */

    /* 0x00E62548-0x00E6257C */
    REM_FILE_$SEND_REQUEST(addr_info, &request, 0x3A,
                           &nil_word, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &nil_word, 0,
                           &nil_word, &packet_id,
                           status);
}
