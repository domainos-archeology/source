/*
 * DIR_$GET_ENTRYU_FUN_00e4d460 - Internal entry retrieval helper
 *
 * Nested Pascal subprocedure of DIR_$GET_ENTRYU (0x00E4D500).  In the
 * original it takes only one explicit argument (the status pointer at
 * A6+0x8); everything else is reached through the parent frame, which it
 * loads with `movea.l (A6),A3` at 0x00E4D46C - A3 is the caller's saved A6,
 * i.e. DIR_$GET_ENTRYU's frame:
 *
 *   (-0x8,A3)  parent's local copy of the directory UID
 *   ( 0xc,A3)  name
 *   (0x10,A3)  pointer to the name length word
 *   (0x14,A3)  entry_ret
 *
 * A5 is inherited, not established here: DIR_$GET_ENTRYU does
 * `lea (0xe7dc00).l,A5` at 0x00E4D508, so A5 = 0xE7DC00, the DIR module
 * data base.  The two A5-relative words this routine reads are therefore
 * 0xE7FCAA (A5+0x20AA) and 0xE7FCAE (A5+0x20AE) - the record+0 / record+4
 * pair of the GET_ENTRYU (op 0x44 >> 1 = 0x22) record of DIR_$OP_TAB,
 * record 0x22 - 21 = 13 of the table at 0xE7FC42 (image 0x0000 / 0x0002):
 * DIR_$OP_REC(DIR_OP_GET_ENTRYU_OP >> 1).version / .base_size.
 *
 * Original address: 0x00E4D460
 * Original size: 160 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$GET_ENTRYU_FUN_00e4d460 - Internal entry retrieval helper
 *
 * Flattened to take explicit parameters in place of the parent-frame
 * accesses listed above.
 *
 * Parameters:
 *   local_uid  - UID of directory (parent's local copy, A3-0x8)
 *   name       - Name to look up (A3+0xC)
 *   name_len   - Name length (*(uint16_t *)(A3+0x10), read at 0x00E4D46E)
 *   entry_ret  - Output: entry information (A3+0x14)
 *   status_ret - Output: status code (A6+0x8)
 */
void DIR_$GET_ENTRYU_FUN_00e4d460(uid_t *local_uid, char *name,
                                   uint16_t name_len, void *entry_ret,
                                   status_$t *status_ret)
{
    /*
     * Request buffer, base A6-0x1B8 (the address actually pushed at
     * 0x00E4D4C2).  Offsets recovered from the stores:
     *   +0x03  op byte          move.b #0x44,(-0x1b5,A6)   0x00E4D494
     *   +0x04  directory UID    two move.l from (-0x8,A3)  0x00E4D49E/A2
     *   +0x0E  table parm word  move.w (0x20aa,A5),(-0x1aa,A6) 0x00E4D4A6
     *   +0x8E  name length      move.w (A0),(-0x12a,A6)    0x00E4D472
     *   +0x90  name bytes       move.b ...,(-0x129,A0)     0x00E4D488
     * DIR_$DO_OP adds 0x8E to req_size (0x00E4C110), so the wire length is
     * 0x8E + name_len + base_size (2) = 0x90 + name_len, exactly
     * through the end of the name.
     */
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /*
     * DIR_$GET_ENTRYU_FUN writes back through entry_ret (A3+0x14) the same
     * three fields the reply carries, in the dir_$old_entry_t layout:
     *   +0x00 <- reply+0x14   move.w (-0x14,A6),(A0)      0x00E4D4D8
     *   +0x02 <- reply+0x16   move.l (A0)+,(0x2,A1)       0x00E4D4E4
     *   +0x06 <- reply+0x1A   move.l (A0)+,(0x6,A1)       0x00E4D4E8
     *   +0x0A <- reply+0x1E   move.l (-0xa,A6),(0xa,A0)   0x00E4D4F0
     */
    dir_$old_entry_t *entry = (dir_$old_entry_t *)entry_ret;
    /* A6-relative 2-byte cell passed as DIR_$DO_OP's fifth argument
     * (pea (-0x1ba,A6) at 0x00E4D4AC, the two bytes immediately below the
     * request buffer); it is REM_FILE_$SEND_REQUEST's `received_len`
     * out-parameter (source-32ld). */
    uint16_t do_op_rcvd_len;
    int16_t i;

    _Static_assert(__builtin_offsetof(dir_$do_op_request_t, op) == 0x03,
                   "get_entryu request.op");
    _Static_assert(__builtin_offsetof(dir_$do_op_request_t, version) == 0x0E,
                   "get_entryu request.version");
    _Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.name.path_len) == 0x8E,
                   "get_entryu request.path_len");
    _Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.name.name) == 0x90,
                   "get_entryu request.name_data");
    _Static_assert(__builtin_offsetof(dir_$old_entry_t, extra) == 0x0A,
                   "get_entryu entry_ret.extra");

    /* Copy name into request buffer (0x00E4D472..0x00E4D490) */
    request.body.name.path_len = name_len;
    for (i = 0; i < (int16_t)name_len; i++) {
        request.body.name.name[i] = name[i];
    }

    /* Build the request (0x00E4D494..0x00E4D4AA) */
    request.op = DIR_OP_GET_ENTRYU_OP;
    request.uid.high = local_uid->high;
    request.uid.low = local_uid->low;
    request.version = DIR_$OP_REC(DIR_OP_GET_ENTRYU_OP >> 1).version; /* (0x20aa,A5) */

    /*
     * Send the request.  Argument order and the two constants are taken
     * straight from the push sequence at 0x00E4D4AC..0x00E4D4C6:
     *   pea (-0x1ba,A6)                       -> received_len
     *   pea (-0x28,A6)                        -> response
     *   move.w #0x22,-(SP)                    -> resp_size
     *   (0x20ae,A5) + (-0x12a,A6)             -> req_size
     *   pea (-0x1b8,A6)                       -> request
     */
    DIR_$DO_OP(&request,
               (int16_t)(name_len + DIR_$OP_REC(DIR_OP_GET_ENTRYU_OP >> 1).base_size),
               0x22,
               &response, &do_op_rcvd_len);

    *status_ret = response.status;

    /* On success copy the entry fields out (0x00E4D4D2..0x00E4D4F6) */
    if (response.status == status_$ok) {
        entry->type = response.entry.word;
        entry->uid.high = response.entry.uid.high;
        entry->uid.low = response.entry.uid.low;
        entry->extra = response.entry.extra;
    }
}
