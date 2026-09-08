/*
 * administrator.c - AUDIT_$ADMINISTRATOR
 *
 * Checks if the current process has audit administrator privileges by
 * resolving the sixteen-character name "`node_data/audit" and checking ACL
 * rights.
 *
 * Original address: 0x00E714B6
 */

#include "audit/audit_internal.h"
#include "name/name.h"
#include "acl/acl.h"

/*
 * Constant cells pooled around AUDIT_$ADMINISTRATOR, addressed with
 * `pea (d,PC)` (PC = instruction address + 2).
 */

/*
 * 0x00E71520, sixteen characters: the pathname NAME_$RESOLVE is given.
 * `pea (0x50,PC)` at 0x00E714CE (0x00E714D0 + 0x50 = 0x00E71520).  Image
 * bytes:
 *
 *   00e71520  60 6e 6f 64 65 5f 64 61  74 61 2f 61 75 64 69 74
 *              `  n  o  d  e  _  d  a   t  a  /  a  u  d  i  t
 *
 * The first byte is 0x60, a BACKQUOTE - the Domain naming-tree shorthand for
 * the local node's entry directory - not two slashes.  The name is counted,
 * not terminated, so the array is exactly sixteen bytes with no NUL.
 */
static const char audit_$admin_path_00e71520[16] = "`node_data/audit";

/*
 * 0x00E7151E, word 0x0010 = 16: that name's length.
 * `pea (0x52,PC)` at 0x00E714CA (0x00E714CC + 0x52 = 0x00E7151E).  Image
 * bytes:
 *
 *   00e71518  ff e8 4e 5e 4e 75 00 10
 *                               ^^^^^
 */
static const int16_t audit_$admin_path_len_00e7151e = 16;

/* 0x00E71498, byte 0x00: ACL_$RIGHTS' ignore_super argument (FALSE - the
 * super-user bypass applies).  `pea (-0x5c,PC)` at 0x00E714F2. */
static const boolean audit_$admin_ignore_super_00e71498 = false;

/* 0x00E71530, longword 0x00000002: the required rights mask.
 * `pea (0x40,PC)` at 0x00E714EE. */
static const uint32_t audit_$admin_rights_mask_00e71530 = 0x00000002;

/* 0x00E71496, word 0x0001: ACL_$RIGHTS' option flags (object type 1).
 * `pea (-0x56,PC)` at 0x00E714EA. */
static const int16_t audit_$admin_acl_opts_00e71496 = 1;

int8_t AUDIT_$ADMINISTRATOR(status_$t *status_ret)
{
    int8_t result = 0;
    uid_t audit_uid;
    uint32_t rights;

    /*
     * 0x00E714C4-0x00E714D8: resolve the audit directory path.  Four
     * by-reference arguments and no result slot ("lea (0x10,SP),SP").
     * NAME_$RESOLVE modifies neither the name nor the length, so the const
     * is cast away to match its prototype.
     */
    NAME_$RESOLVE((char *)audit_$admin_path_00e71520,
                  (int16_t *)&audit_$admin_path_len_00e7151e,
                  &audit_uid, status_ret);

    if (*status_ret == status_$ok) {
        /* Check if caller has administrative rights */
        rights = ACL_$RIGHTS(&audit_uid,
                             (boolean *)&audit_$admin_ignore_super_00e71498,
                             (uint32_t *)&audit_$admin_rights_mask_00e71530,
                             (int16_t *)&audit_$admin_acl_opts_00e71496,
                             status_ret);

        /* 0x00E71504 `cmpi.l #0x2,D0` + `seq`: the whole longword result. */
        if (rights == 2) {
            result = (int8_t)-1;  /* 0xFF = true */
        }
    } else {
        /* 0x00E714E0 move.l #0x30000c,(A2) */
        *status_ret = status_$audit_could_not_find_audit_event_list;
    }

    return result;
}
