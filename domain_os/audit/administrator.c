/*
 * administrator.c - AUDIT_$ADMINISTRATOR
 *
 * Checks if the current process has audit administrator privileges
 * by resolving //node_data/audit and checking ACL rights.
 *
 * Original address: 0x00E714B6
 */

#include "audit/audit_internal.h"
#include "name/name.h"
#include "acl/acl.h"

/* Path to audit directory for rights check */
static const char audit_path[] = "//node_data/audit";
static int16_t audit_path_len = 16;

/*
 * Constant cells pooled around AUDIT_$ADMINISTRATOR, addressed with
 * `pea (d,PC)` (PC = instruction address + 2).
 */

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

    /* Resolve the audit directory path */
    /* NAME_$RESOLVE does not modify the path; cast away const of the
     * static string to match its char* prototype. */
    NAME_$RESOLVE((char *)audit_path, &audit_path_len, &audit_uid, status_ret);

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
        /* Audit file not found */
        *status_ret = status_$audit_file_not_found;
    }

    return result;
}
