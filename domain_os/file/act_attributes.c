/*
 * FILE_$ACT_ATTRIBUTES - Get active file attributes (old format)
 *
 * Identical to FILE_$ATTRIBUTES except for the flag word it hands
 * AST_$GET_ATTRIBUTES: 0x01 instead of 0x21, i.e. no remote refresh.
 *
 * Original address: 0x00E5DAB0, 98 bytes.
 *
 * Frame (`link.w A6,-0xbc`, no register saves):
 *   A6+0x08  file_uid    caller's object UID
 *   A6+0x0C  attr_out    old-format VTOCE the result is written into
 *   A6+0x10  status_ret
 *
 *   A6-0xBC  status_$t status
 *   A6-0xB8  uid_t     uid_tmp         Pascal temporary (0x00E5DAB4)
 *   A6-0xB0  uint8_t   attrs[0x90]
 *   A6-0x20  file_$obj_loc_t desc      UID at A6-0x18, flags byte at A6-0x03
 */

#include "file/file_internal.h"

/*
 * The same constant cell at 0xE5DAAE that FILE_$ATTRIBUTES uses, reached here
 * with `pea (-0x4c,PC)` at 0x00E5DAF8 (0x00E5DAF8 + 2 - 0x4C = 0xE5DAAE).
 * The image holds 0xFF - VTOCE_$NEW_TO_OLD's "use the +0x04 parent UID"
 * boolean (0x00E38502).  It is a file-static in each translation unit because
 * C has no way to share one code-region literal between two objects.
 */
static char file_$attr_new_to_old_flags = (char)0xFF;

/* `move.w #0x1,-(SP)` at 0x00E5DADC - AST_$GET_ATTRIBUTES' flag word. */
#define FILE_ACT_ATTRIBUTES_FLAGS   0x0001

void FILE_$ACT_ATTRIBUTES(uid_t *file_uid, void *attr_out, status_$t *status_ret)
{
    status_$t       status;                     /* A6-0xBC */
    uid_t           uid_tmp;                    /* A6-0xB8 */
    uint8_t         attrs[AST_ATTR_REC_SIZE];   /* A6-0xB0 */
    file_$obj_loc_t desc;                       /* A6-0x20 */

    /* 0x00E5DAB4-0x00E5DABE */
    uid_tmp.high = file_uid->high;
    uid_tmp.low  = file_uid->low;

    /* 0x00E5DAC0-0x00E5DACA */
    desc.uid.high = uid_tmp.high;
    desc.uid.low  = uid_tmp.low;

    /* 0x00E5DACC: `bclr.b #0x6,(-0x3,A6)` - descriptor+0x1D bit 6. */
    desc.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

    /* 0x00E5DAD2-0x00E5DAEA */
    AST_$GET_ATTRIBUTES(&desc, FILE_ACT_ATTRIBUTES_FLAGS, attrs, &status);

    /* 0x00E5DAEE-0x00E5DB04 */
    if (status == status_$ok) {
        VTOCE_$NEW_TO_OLD(attrs, &file_$attr_new_to_old_flags, attr_out);
    }

    /* 0x00E5DB06 */
    *status_ret = status;
}
