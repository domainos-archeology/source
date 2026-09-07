/*
 * FILE_$ATTRIBUTES - Get file attributes (old format)
 *
 * Original address: 0x00E5DA4C, 98 bytes.
 *
 * Frame (`link.w A6,-0xbc`, no register saves):
 *   A6+0x08  file_uid    caller's object UID
 *   A6+0x0C  attr_out    old-format VTOCE the result is written into
 *   A6+0x10  status_ret
 *
 *   A6-0xBC  status_$t status          AST_$GET_ATTRIBUTES' status
 *   A6-0xB8  uid_t     uid_tmp         the Pascal temporary the caller's UID
 *                                      is copied into before it is copied on
 *                                      into the descriptor (0x00E5DA50)
 *   A6-0xB0  uint8_t   attrs[0x90]     the full attribute record
 *   A6-0x20  file_$obj_loc_t desc      the object-location record; its UID
 *                                      field is A6-0x18 (= -0x20+0x08) and
 *                                      its flags byte is A6-0x03 (= -0x20+0x1D)
 * The attribute record and the descriptor are adjacent, exactly as in
 * AST_$GET_COMMON_ATTRIBUTES' own frame.
 */

#include "file/file_internal.h"

/*
 * Constant cell at 0xE5DAAE (the two bytes between this function's `rts` and
 * FILE_$ACT_ATTRIBUTES' entry), passed by reference as VTOCE_$NEW_TO_OLD's
 * second (var) argument with `pea (0x18,PC)` at 0x00E5DA94.  The image holds
 * 0xFF, i.e. "take the parent UID from the new record's +0x04 alias rather
 * than from +0x88" (VTOCE_$NEW_TO_OLD 0x00E38502).  FILE_$ACT_ATTRIBUTES
 * shares the same cell, via `pea (-0x4c,PC)` at 0x00E5DAF8.
 */
static char file_$attr_new_to_old_flags = (char)0xFF;

/* `move.w #0x21,-(SP)` at 0x00E5DA78 - AST_$GET_ATTRIBUTES' flag word:
 * bit 5 = refresh a remote object, bit 0 = the caller holds a lock. */
#define FILE_ATTRIBUTES_FLAGS   0x0021

void FILE_$ATTRIBUTES(uid_t *file_uid, void *attr_out, status_$t *status_ret)
{
    status_$t       status;                     /* A6-0xBC */
    uid_t           uid_tmp;                    /* A6-0xB8 */
    uint8_t         attrs[AST_ATTR_REC_SIZE];   /* A6-0xB0 */
    file_$obj_loc_t desc;                       /* A6-0x20 */

    /* 0x00E5DA50-0x00E5DA5A: the caller's UID into the Pascal temporary. */
    uid_tmp.high = file_uid->high;
    uid_tmp.low  = file_uid->low;

    /* 0x00E5DA5C-0x00E5DA66: and on into the descriptor at +0x08. */
    desc.uid.high = uid_tmp.high;
    desc.uid.low  = uid_tmp.low;

    /* 0x00E5DA68: `bclr.b #0x6,(-0x3,A6)` - descriptor+0x1D bit 6. */
    desc.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

    /* 0x00E5DA6E-0x00E5DA86.  The `subq.l #0x2,SP` is AST_$GET_ATTRIBUTES'
     * unused Pascal result slot. */
    AST_$GET_ATTRIBUTES(&desc, FILE_ATTRIBUTES_FLAGS, attrs, &status);

    /* 0x00E5DA8A-0x00E5DAA0 */
    if (status == status_$ok) {
        VTOCE_$NEW_TO_OLD(attrs, &file_$attr_new_to_old_flags, attr_out);
    }

    /* 0x00E5DAA2 */
    *status_ret = status;
}
