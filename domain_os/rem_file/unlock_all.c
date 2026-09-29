/*
 * rem_file/unlock_all.c - REM_FILE_$UNLOCK_ALL (0x00E61C72, 164 bytes)
 *
 * Broadcasts an "unlock everything I hold" message; there is no reply socket
 * and no reply, so the request goes straight to PKT_$SEND_INTERNET rather
 * than through REM_FILE_$SEND_REQUEST.
 *
 * Re-emitted against the listing for bead source-s5n9: the data pointer is
 * `pea (0x34,PC)` at 0x00E61CE2 (extension word 0x00E61CE4 + 0x34), i.e.
 * &REM_FILE_$NIL_CONST at 0x00E61D18, not NULL; and the packet-info template
 * is REM_FILE_$DATA (the SAU2 map's name for 0x00E2E380).
 *
 * Frame: `link.w A6,-0xd8` + `pea (A2)` (4 bytes), so the epilogue is
 * `movea.l (-0xdc,A6),A2` (0x00E61D0E).  SP is never rebalanced after the
 * call - `unlk` does it.
 */

#include "rem_file/rem_file_internal.h"
#include "pkt/pkt.h"

/*
 * The request, 0x10 bytes at A6-0xD0 (`move.w #0x10,-(SP)` at 0x00E61CE6).
 * Unlike every other builder this one stamps its own message type, because
 * REM_FILE_$SEND_REQUEST is not involved.
 */
typedef struct rem_file_$unlock_all_req_t {
    uint16_t    msg_type;       /* 0x00: 1     (0x00E61C78) */
    uint8_t     magic;          /* 0x02: 0x80  (0x00E61C7E) */
    uint8_t     opcode;         /* 0x03: 0x12  (0x00E61C84) */
    uid_t       nil_uid;        /* 0x04: UID_$NIL, 0x00E1737C (0x00E61C90) */
    uint16_t    reserved;       /* 0x0C: 3     (0x00E61C98) */
    int8_t      admin_flag;     /* 0x0E:       (0x00E61CB2) */
    uint8_t     _pad_0f;        /* 0x0F */
} rem_file_$unlock_all_req_t;

_Static_assert(__builtin_offsetof(rem_file_$unlock_all_req_t, nil_uid) == 0x04, "unlock_all_req.nil_uid");
_Static_assert(__builtin_offsetof(rem_file_$unlock_all_req_t, reserved) == 0x0C, "unlock_all_req.reserved");
_Static_assert(__builtin_offsetof(rem_file_$unlock_all_req_t, admin_flag) == 0x0E, "unlock_all_req.admin_flag");
_Static_assert(sizeof(rem_file_$unlock_all_req_t) == 0x10, "unlock_all_req size");

/* The number of bytes of REM_FILE_$DATA copied into the frame: seven
 * longwords then a word (`moveq #0x6,D0` / `dbf` / `move.w (A1)+,(A2)+`
 * at 0x00E61CC0-0x00E61CCA). */
#define UNLOCK_ALL_PKT_INFO_LEN 30

/* The file server's well-known socket, and the source socket this broadcast
 * claims (0x00E61D00 / 0x00E61CF4). */
#define UNLOCK_ALL_DEST_SOCKET  2
#define UNLOCK_ALL_SRC_SOCKET   9999   /* 0x270F */

void REM_FILE_$UNLOCK_ALL(void)
{
    rem_file_$unlock_all_req_t request;             /* A6-0xD0 */
    uint8_t   pkt_info[UNLOCK_ALL_PKT_INFO_LEN];    /* A6-0x20 */
    uint16_t  retry_hint;                           /* A6-0xD8 */
    uint16_t  timeout_out;                          /* A6-0xD6 */
    status_$t status;                               /* A6-0xD4 */
    int       i;

    request.msg_type = 1;                       /* 0x00E61C78 */
    request.magic    = REM_FILE_REQ_MAGIC;      /* 0x00E61C7E */
    request.opcode   = REM_FILE_OP_UNLOCK_ALL;  /* 0x00E61C84 */
    request.nil_uid  = UID_$NIL;                /* 0x00E61C90 */
    request.reserved = 3;                       /* 0x00E61C98 */
    /* 0x00E61C9E-0x00E61CB2: `sgt` on ACL_$UNWIRED_DATA.super_count[PROC1_$CURRENT]. */
    request.admin_flag = REM_FILE_PROCESS_HAS_ADMIN() ? true : false;

    /* 0x00E61CB6-0x00E61CCA: copy the client packet-info template, then
     * 0x00E61CCC `bset.b #0x7,(-0x1f,A6)` sets bit 7 of byte 1 - the
     * broadcast bit. */
    for (i = 0; i < UNLOCK_ALL_PKT_INFO_LEN; i++) {
        pkt_info[i] = REM_FILE_$DATA[i];
    }
    pkt_info[1] |= 0x80;

    /* 0x00E61CD2-0x00E61D08.  Fifteen arguments plus a 2-byte result slot;
     * the destination node is 0, which is what makes it a broadcast. */
    PKT_$SEND_INTERNET(0,                       /* routing_key */
                       0,                       /* dest_node - broadcast */
                       UNLOCK_ALL_DEST_SOCKET,
                       0,                       /* src_node_or */
                       NODE_$ME,
                       UNLOCK_ALL_SRC_SOCKET,
                       pkt_info,
                       0,                       /* request_id */
                       &request,
                       0x10,                    /* template_len */
                       &REM_FILE_$NIL_CONST,    /* data - the cell at
                                                 * 0x00E61D18 */
                       0,                       /* data_len */
                       &retry_hint,
                       &timeout_out,
                       &status);
}
