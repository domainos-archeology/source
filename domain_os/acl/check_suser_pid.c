/*
 * acl_$check_suser_pid - Check if a process has superuser privileges
 *
 * Checks if the specified process is:
 *   - PID 1 (always superuser)
 *   - In super mode (SUPER_COUNT > 0)
 *   - Has login UID matching RGYC_$G_LOGIN_UID
 *   - Has user/group/login UID matching RGYC_$G_LOCKSMITH_UID
 *
 * If superuser, sets the corresponding bit in ASID_SUSER_BITMAP.
 *
 * Parameters:
 *   pid - Process ID to check
 *
 * Returns:
 *   Non-zero (0xFF) if superuser, 0 otherwise
 *
 * Original address: 0x00E463E4
 */

#include "acl/acl_internal.h"

/*
 * Helper to compare two UIDs
 */
static int8_t uid_equal(uid_t *a, uid_t *b)
{
    return (a->high == b->high && a->low == b->low) ? -1 : 0;
}

int8_t acl_$check_suser_pid(int16_t pid)
{
    acl_sid_block_t *sids;
    int8_t is_suser = 0;

    sids = &ACL_$CURRENT_SIDS[pid];

    /*
     * Check conditions for superuser status:
     * 1. PID 1 is always superuser
     * 2. Process is in super mode (SUPER_COUNT > 0)
     * 3. Login UID matches RGYC_$G_LOGIN_UID
     * 4. User SID matches RGYC_$G_LOCKSMITH_UID
     * 5. Group SID matches RGYC_$G_LOCKSMITH_UID
     * 6. Login SID matches RGYC_$G_LOCKSMITH_UID
     */
    if (pid == 1 ||
        ACL_$SUPER_COUNT[pid] > 0 ||
        uid_equal(&sids->login_sid, &RGYC_$G_LOGIN_UID) ||
        uid_equal(&sids->user_sid, &RGYC_$G_LOCKSMITH_UID) ||
        uid_equal(&sids->group_sid, &RGYC_$G_LOCKSMITH_UID) ||
        uid_equal(&sids->login_sid, &RGYC_$G_LOCKSMITH_UID)) {

        is_suser = -1;  /* 0xFF */

        /* Mark this ASID as having used suser privilege */
        ACL_$ASID_SUSER_BITMAP[(pid - 1) >> 3] |= (0x80 >> ((pid - 1) & 7));
    }

    return is_suser;
}
