/*
 * acl_$expand_default_acl - build a protection record from a "default ACL" UID
 *
 * Original address: 0x00E45984 (was FUN_00e45984), 220 bytes.
 *
 * A default ACL is not an object at all: its whole definition is encoded in
 * the ACL UID itself.  The high word of uid.high is the type - 1 for a file
 * ACL, 2 for a directory ACL - and the low word of uid.high carries the rights
 * bits.  ACL_$NIL (0x00E17384) is treated as an alias for ACL_$FNDWRX
 * (0x00E174C4) before the type is looked at.
 *
 * The only caller is acl_$load_acl_image (0x00E45A70), which pushes
 * (prot, acl_uid) so that the callee sees A6+0x08 = acl_uid, A6+0x0C = prot.
 * The call is a plain `bsr.w` with no static link (`movea.l (A6),An` never
 * appears and the routine reaches its globals through A5), so this is a
 * module-level Pascal procedure, not a nested one - hence its own file.
 *
 * Frame (A6+):
 *   0x08 acl_uid -> A3
 *   0x0C prot    -> A2
 *
 * Locals (A6-):
 *   -0x1e rights_w   word, the rights bits being massaged
 *   -0x1c conv       longword handed to acl_$convert_rights
 *   -0x18 enc        8 bytes, the encoded ACL UID (or ACL_$FNDWRX)
 *   -0x10 acl_type   8 bytes, ACL_$FILE_ACL or ACL_$DIR_ACL
 *
 * Returns a Domain boolean: false (0x00) when the UID is a real ACL object
 * that has to be mapped, true (0xFF, `st D0b` at 0x00E45A54) when `prot` has
 * been filled in from the encoded rights.
 */

#include "acl/acl_internal.h"
#include "uid/uid.h"

boolean acl_$expand_default_acl(uid_t *acl_uid, acl_$prot_data_t *prot)
{
    uid_t    enc;           /* A6-0x18 */
    uid_t    acl_type;      /* A6-0x10 */
    uint16_t rights_w;      /* A6-0x1E */
    uint32_t conv;          /* A6-0x1C */

    /* 0x00E45994-0x00E459B4: ACL_$NIL stands in for ACL_$FNDWRX. */
    if (acl_$uid_eq(acl_uid, &ACL_$NIL)) {
        enc = ACL_$FNDWRX;
    } else {
        /* 0x00E459B6-0x00E459D2 */
        enc = *acl_uid;
        if ((int16_t)(enc.high >> 16) != ACL_DEFAULT_TYPE_FILE &&
            (int16_t)(enc.high >> 16) != ACL_DEFAULT_TYPE_DIR) {
            return false;                               /* `clr.b D0b` */
        }
    }

    /*
     * 0x00E459D6: ACL_$DEF_ACLDATA(prot, acl_uid).  Its second argument is an
     * OUT parameter - the routine stores UID_$NIL there (0x00E47948) - so this
     * call also clears the caller's ACL UID.  That is deliberate: the encoded
     * UID has already been copied into `enc`, and acl_$load_acl_image's other
     * default-ACL exit does the same store explicitly (0x00E45E04).
     */
    ACL_$DEF_ACLDATA(prot, acl_uid);

    /* 0x00E459E0: the rights live in the low word of enc.high. */
    rights_w = (uint16_t)(enc.high & 0xFFFFu);

    /*
     * 0x00E459E6-0x00E45A04.  `btst.b #0x5,(-0x1e,A6)` addresses the HIGH byte
     * of the word, so the bit under test is 0x2000.
     */
    if ((rights_w & ACL_DEFAULT_RIGHTS_LITERAL) != 0) {
        rights_w = (uint16_t)(rights_w & (uint16_t)~ACL_DEFAULT_RIGHTS_LITERAL);
    } else if ((int16_t)(enc.high >> 16) == ACL_DEFAULT_TYPE_DIR) {
        rights_w = (uint16_t)(rights_w | ACL_DEFAULT_RIGHTS_DIR_ADD);
    }

    /*
     * 0x00E45A04-0x00E45A1A: `and.w` against #0x3fff, then the longword at
     * A6-0x1C has its low three bytes replaced and its top byte cleared
     * (`andi.l #-0x1000000` / `or.l D0` / `clr.b`).  Whatever A6-0x1C held on
     * entry is masked away by the final `clr.b`, so the result is exactly the
     * masked rights; `conv` is initialised here only to keep the host build
     * deterministic.
     */
    conv = 0;
    conv &= 0xFF000000u;
    conv |= (uint32_t)(rights_w & ACL_DEFAULT_RIGHTS_MASK);
    conv &= 0x00FFFFFFu;

    /* 0x00E45A1E-0x00E45A38 */
    if ((int16_t)(enc.high >> 16) == ACL_DEFAULT_TYPE_FILE) {
        acl_type = ACL_$FILE_ACL;
    } else {
        acl_type = ACL_$DIR_ACL;
    }

    /* 0x00E45A3C-0x00E45A50 */
    prot->world_rights = acl_$convert_rights(conv | ACL_CONVERT_RIGHTS_DEFAULT,
                                             &acl_type);
    return true;                                        /* `st D0b` */
}
