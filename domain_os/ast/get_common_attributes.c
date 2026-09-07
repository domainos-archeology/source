/*
 * AST_$GET_COMMON_ATTRIBUTES - distil the common attribute summary
 *
 * Asks AST_$GET_ATTRIBUTES for the full 0x90-byte attribute record and copies
 * the handful of fields most callers want into an ast_$common_attr_t.
 *
 * Original address: 0x00e04a00, 170 bytes.
 *
 * Frame: `link.w A6,-0x90` - the whole frame IS the full attribute record, so
 * a displacement d in the listing is record offset d+0x90.  The record is a
 * verbatim copy of aote+0x0C (AST_$GET_ATTRIBUTES at 0x00E049A2), which is
 * what makes the field names below meaningful.
 *
 * A5 is loaded (`lea (0xe1dc80).l,A5` at 0x00E04A08) but no A5 global is
 * touched; it is established for AST_$GET_ATTRIBUTES.
 *
 * Arguments (0x00E04A10-0x00E04A20, pushed right to left for the inner call):
 *   A6+0x08 uid / object-location descriptor   -> AST_$GET_ATTRIBUTES arg 1
 *   A6+0x0C flags word                         -> arg 2
 *   A6+0x0E out (A2, the ast_$common_attr_t)
 *   A6+0x12 status                             -> arg 4
 */

#include "ast/ast_internal.h"

/*
 * Offsets inside the full record that this function reads.  Each one is the
 * listing's A6 displacement plus 0x90.
 */
#define AST_ATTR_TYPE_WORDS     0x00    /* -0x90: obj_type/sub_type/flags */
#define AST_ATTR_COUNTER_20     0x14    /* -0x7C: aote+0x20 */
#define AST_ATTR_MOD_TIME       0x3C    /* -0x54: aote+0x48, 12 bytes */
#define AST_ATTR_REFCOUNT       0x74    /* -0x1C: aote+0x80 */
#define AST_ATTR_ACCESS_FLAGS   0x65    /* -0x2B: aote+0x71 */

void AST_$GET_COMMON_ATTRIBUTES(uid_t *uid, uint16_t flags,
                                ast_$common_attr_t *attrs, status_$t *status)
{
    uint8_t full_attrs[AST_ATTR_REC_SIZE];      /* A6-0x90 */
    uint8_t *out = (uint8_t *)attrs;
    int8_t   access_flags;
    int      i;

    /* 0x00E04A0E-0x00E04A28.  The `subq.l #0x2,SP` is the Pascal result slot
     * of AST_$GET_ATTRIBUTES, whose value this procedure ignores. */
    AST_$GET_ATTRIBUTES(uid, flags, full_attrs, status);

    /* 0x00E04A2C: one longword - obj_type, sub_type and the two attribute
     * flag bytes. */
    for (i = 0; i < 4; i++) {
        out[i] = full_attrs[AST_ATTR_TYPE_WORDS + i];
    }

    /* 0x00E04A30: record+0x14 (aote+0x20) into the record's +0x04. */
    for (i = 0; i < 4; i++) {
        out[0x04 + i] = full_attrs[AST_ATTR_COUNTER_20 + i];
    }

    /* 0x00E04A36-0x00E04A42: `moveq #0xb` + dbf = 12 bytes, the modification
     * time (attribute 5) followed by the block count (attribute 0x0B). */
    for (i = 0; i < 12; i++) {
        out[0x08 + i] = full_attrs[AST_ATTR_MOD_TIME + i];
    }

    /* 0x00E04A46: the object reference count, record+0x74 = aote+0x80. */
    for (i = 0; i < 2; i++) {
        out[0x14 + i] = full_attrs[AST_ATTR_REFCOUNT + i];
    }

    access_flags = (int8_t)full_attrs[AST_ATTR_ACCESS_FLAGS];

    /* 0x00E04A4C-0x00E04A5C: `tst.b` + `smi` - bit 7 of the source byte,
     * tested as a signed byte, becomes bit 7 of attrs->access_flags. */
    out[0x16] &= (uint8_t)~AST_CATTR_OS_ONLY;
    if (access_flags < 0) {
        out[0x16] |= AST_CATTR_OS_ONLY;
    }

    /* 0x00E04A60-0x00E04A72: bit 6 likewise. */
    out[0x16] &= (uint8_t)~AST_CATTR_MODE_BIT6;
    if ((access_flags & 0x40) != 0) {
        out[0x16] |= AST_CATTR_MODE_BIT6;
    }

    /* 0x00E04A76-0x00E04A88: bit 5 of the source byte lands in bit 1 of the
     * high attribute-flags byte, overwriting what came from aote+0x0E. */
    out[0x02] &= (uint8_t)~AST_CATTR_HI_MODE_BIT5;
    if ((access_flags & 0x20) != 0) {
        out[0x02] |= AST_CATTR_HI_MODE_BIT5;
    }

    /* 0x00E04A8C-0x00E04A9C: bit 4 lands in bit 0 of the same byte. */
    out[0x02] &= (uint8_t)~AST_CATTR_HI_MODE_BIT4;
    if ((access_flags & 0x10) != 0) {
        out[0x02] |= AST_CATTR_HI_MODE_BIT4;
    }
}
