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

/* Required rights mask for administrator access */
static uint32_t admin_rights_mask = 0;  /* Filled in from original binary */
static int16_t admin_rights_option = 0;

int8_t AUDIT_$ADMINISTRATOR(status_$t *status_ret)
{
    int8_t result = 0;
    uid_t audit_uid;
    int rights;

    /* Resolve the audit directory path */
    NAME_$RESOLVE(audit_path, &audit_path_len, &audit_uid, status_ret);

    if (*status_ret == status_$ok) {
        /* Check if caller has administrative rights */
        rights = ACL_$RIGHTS(&audit_uid, &admin_rights_mask,
                             &admin_rights_mask, &admin_rights_option, status_ret);

        /* Rights value of 2 indicates administrator access */
        if (rights == 2) {
            result = (int8_t)-1;  /* 0xFF = true */
        }
    } else {
        /* Audit file not found */
        *status_ret = status_$audit_file_not_found;
    }

    return result;
}
