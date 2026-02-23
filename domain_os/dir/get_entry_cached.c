/*
 * dir_$get_entry_cached - Cached directory entry lookup
 *
 * Originally a nested Pascal subprocedure of dir_$do_op_get_entryu.
 * Implements a per-UID hash cache (modulo 111 entries) that accelerates
 * repeated directory entry lookups. Each cache entry stores:
 *   - Directory UID (8 bytes, at offset 0x400)
 *   - Entry UID (8 bytes, at offset 0x408)
 *   - DTV data (6 bytes, at offset 0x410)
 *   - Entry name with flags (at offset 0x416)
 *
 * The cache is stored in the A5-relative global data area. The hash is
 * computed from the directory UID low word and all name bytes, then
 * reduced modulo 0x6F (111). Cache entries are 0x28 (40) bytes each.
 *
 * On cache miss, calls dir_$lookup_entry to do the actual lookup and
 * populates the cache with the result if the entry is a cacheable type
 * (found_ret is set by lookup_entry).
 *
 * Parameters:
 *   uid        - Directory UID
 *   name       - Entry name
 *   name_len   - Length of name (max 17; longer names bypass cache)
 *   type_ret   - Output: entry type
 *   uid_ret    - Output: entry UID (8 bytes)
 *   extra_ret  - Output: extra data
 *   status_ret - Output: status code
 *
 * Original address: 0x00E4CD90
 * Original size: 612 bytes
 */

#include "dir/dir_internal.h"

/* ACL rights parameters for cache hit path */
extern uint8_t DAT_00e4cff4;
extern uint8_t DAT_00e4cff6;

void dir_$get_entry_cached(uid_t *uid, void *name, uint16_t name_len,
                           short *type_ret, char *uid_ret, uint32_t *extra_ret,
                           status_$t *status_ret)
{
    char *a5 = (char *)__A5_BASE();
    uint8_t *name_bytes = (uint8_t *)name;
    uint8_t found_ret;
    uint32_t dtv_data[2];  /* 6 bytes: 4-byte + 2-byte */
    uint16_t dtv_word;

    /* Names longer than 17 chars bypass the cache */
    if (name_len > 0x11) {
        *(int32_t *)(a5 + 0x2010) += 1;  /* Increment bypass counter */
        dir_$lookup_entry(uid, name, name_len, type_ret, uid_ret,
                          extra_ret, &found_ret, status_ret);
        return;
    }

    *(int32_t *)(a5 + 0x2018) += 1;  /* Increment lookup counter */

    /* Get DTV (directory tree version) for comparison */
    uint32_t dtv_param = 0;
    AST_$GET_DTV(*(uint32_t *)(uid), 0, dtv_data, status_ret);
    if (*status_ret == file_$object_not_found) {
        *status_ret = status_$naming_ran_out_of_address_space;
    }
    if (*status_ret != status_$ok) {
        return;
    }

    /* Compute hash from UID low word and all name bytes */
    uint16_t hash_val = *(uint16_t *)((char *)uid + 2);  /* UID low word */
    if (name_len != 0) {
        int16_t remaining = name_len - 1;
        uint16_t idx = 1;
        do {
            hash_val += (uint16_t)name_bytes[idx - 1];
            idx++;
            remaining--;
        } while (remaining != -1);
    }

    /* Lock the directory mutex for cache access */
    ML_$EXCLUSION_START(&DIR_$MUTEX);

    /* Cache entry base: A5 + (hash % 111) * 0x28 */
    int cache_idx = (uint32_t)hash_val % 0x6F;
    char *cache_entry = a5 + cache_idx * 0x28;

    /* Check cache hit: compare name length, UID, DTV, and name bytes */
    uint8_t cached_name_len = (uint8_t)(*(uint8_t *)(cache_entry + 0x416) >> 2);
    if (cached_name_len != name_len) {
        goto cache_miss;
    }

    /* Compare directory UID (8 bytes) */
    if (*(uint32_t *)(cache_entry + 0x400) != uid->high ||
        *(uint32_t *)(cache_entry + 0x404) != uid->low) {
        goto cache_miss;
    }

    /* Compare entry name bytes */
    if (name_len != 0) {
        int16_t remaining = name_len - 1;
        uint32_t i = 1;
        do {
            if (*(char *)(cache_entry + i + 0x416) != (char)name_bytes[i - 1]) {
                goto cache_miss;
            }
            i++;
            remaining--;
        } while (remaining != -1);
    }

    /* Compare DTV (6 bytes at cache offset 0x410) */
    {
        int16_t *p1 = (int16_t *)dtv_data;
        int16_t *p2 = (int16_t *)(cache_entry + 0x410);
        int16_t count = 2;
        int16_t v1, v2;
        do {
            v2 = *p2++;
            v1 = *p1++;
            if (v1 != v2) goto cache_miss;
            count--;
        } while (count != -1);
    }

    /* Cache hit */
    *type_ret = 1;
    {
        uint32_t *uid_out = (uint32_t *)uid_ret;
        uid_out[0] = *(uint32_t *)(cache_entry + 0x408);
        uid_out[1] = *(uint32_t *)(cache_entry + 0x40C);
    }
    *extra_ret = 0;
    *(int32_t *)(a5 + 0x201C) += 1;  /* Increment hit counter */

    /* Check if cache entry has the "ACL checked" flag (bit 8) */
    if ((*(uint16_t *)(cache_entry + 0x416) & 0x100) != 0) {
        /* ACL already checked - done */
        *(int32_t *)(a5 + 0x2014) += 1;  /* Increment ACL-cached counter */
        ML_$EXCLUSION_STOP(&DIR_$MUTEX);
        return;
    }

    /* Need to check ACL rights */
    ML_$EXCLUSION_STOP(&DIR_$MUTEX);
    ACL_$RIGHTS(uid, &DAT_00e4cff4, &DAT_00e4cff6, &DAT_00e4b444, status_ret);
    if (*status_ret == status_$ok) {
        return;
    }
    NAME_CONVERT_ACL_STATUS(status_ret);
    *(uint32_t *)uid_ret = 0;  /* Clear result UID on ACL failure */
    return;

cache_miss:
    ML_$EXCLUSION_STOP(&DIR_$MUTEX);

    /* Perform uncached lookup */
    dir_$lookup_entry(uid, name, name_len, type_ret, uid_ret,
                      extra_ret, &found_ret, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }
    if ((int8_t)found_ret >= 0) {
        /* Not cache-eligible */
        return;
    }

    /* Populate cache entry */
    uint16_t min_rights = ACL_$MIN_RIGHTS(uid);

    ML_$EXCLUSION_START(&DIR_$MUTEX);

    /* Copy UID to cache */
    *(uint32_t *)(cache_entry + 0x400) = uid->high;
    *(uint32_t *)(cache_entry + 0x404) = uid->low;

    /* Copy result UID to cache */
    {
        uint32_t *uid_src = (uint32_t *)uid_ret;
        *(uint32_t *)(cache_entry + 0x408) = uid_src[0];
        *(uint32_t *)(cache_entry + 0x40C) = uid_src[1];
    }

    /* Copy DTV to cache */
    *(uint32_t *)(cache_entry + 0x410) = dtv_data[0];
    *(uint16_t *)(cache_entry + 0x414) = *(uint16_t *)((char *)dtv_data + 4);

    /* Store name length (shifted left 2 bits) and name bytes */
    *(uint8_t *)(cache_entry + 0x416) = *(uint8_t *)(cache_entry + 0x416) & 3;
    *(uint8_t *)(cache_entry + 0x416) = ((uint8_t)name_len << 2) |
                                         *(uint8_t *)(cache_entry + 0x416);

    /* Store min_rights flag in bit 0 */
    *(uint8_t *)(cache_entry + 0x416) = *(uint8_t *)(cache_entry + 0x416) & 0xFE;
    *(uint8_t *)(cache_entry + 0x416) = (uint8_t)(-((min_rights & 1) != 0)) >> 7 |
                                         *(uint8_t *)(cache_entry + 0x416);

    /* Copy name bytes to cache */
    if (name_len != 0) {
        int16_t remaining = name_len - 1;
        uint16_t i = 1;
        do {
            *(uint8_t *)(cache_entry + i + 0x416) = name_bytes[i - 1];
            i++;
            remaining--;
        } while (remaining != -1);
    }

    ML_$EXCLUSION_STOP(&DIR_$MUTEX);
}
