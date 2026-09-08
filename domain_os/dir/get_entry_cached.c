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
 *   uid_ret    - Output: entry UID
 *   extra_ret  - Output: extra data
 *   status_ret - Output: status code
 *
 * Original address: 0x00E4CD90
 * Original size: 612 bytes
 */

#include "dir/dir_internal.h"

/*
 * Constant cells for the ACL_$RIGHTS call at 0x00E4CF06, addressed with
 * `pea (d,PC)` (PC = instruction address + 2).
 */

/* 0x00E4CFF4, byte 0x00: ACL_$RIGHTS' ignore_super argument (FALSE - the
 * super-user bypass applies).  `pea (0xf4,PC)` at 0x00E4CEFE. */
static const boolean dir_$get_entry_ignore_super_00e4cff4 = false;

/* 0x00E4CFF6, longword 0x00000001: the required rights mask.
 * `pea (0xfa,PC)` at 0x00E4CEFA. */
static const uint32_t dir_$get_entry_rights_00e4cff6 = 0x00000001;

/* 0x00E4B444, word 0x0001: ACL_$RIGHTS' option flags (object type 1,
 * directory).  `pea (-0x1ab4,PC)` at 0x00E4CEF6. */

/* ACL rights parameters for cache hit path */

void dir_$get_entry_cached(uid_t *uid, void *name, uint16_t name_len,
                           uint16_t *type_ret, uid_t *uid_ret,
                           uint32_t *extra_ret, status_$t *status_ret)
{
    char *blk = DIR_$BLOCK;   /* the routine's own A5 = 0x00E7DC00 */
    uint8_t *name_bytes = (uint8_t *)name;
    uint8_t found_ret;
    uint32_t dtv_data[2];  /* 6 bytes: 4-byte + 2-byte */
    uint16_t dtv_word;

    /* 0x00E4CD9E: names longer than 17 bytes bypass the cache entirely. */
    if (name_len > DIR_ENTRY_CACHE_MAX_NAME) {
        DIR_ENTRY_CACHE_TOO_LONG_NAME_OF(blk) += 1;
        dir_$lookup_entry(uid, name, name_len, type_ret, uid_ret,
                          extra_ret, &found_ret, status_ret);
        return;
    }

    DIR_ENTRY_CACHE_TRIES_OF(blk) += 1;

    /* Get DTV (directory tree version) for comparison.
     * 0x00E4CDCC pushes the enclosing frame's UID *pointer*
     * (`move.l (0x8,A3),-(SP)`), and 0x00E4CDC8 pushes the zeroed cell at
     * A6-0x0C by value as argument 2. */
    uint32_t dtv_param = 0;
    AST_$GET_DTV(uid, dtv_param, dtv_data, status_ret);
    if (*status_ret == file_$object_not_found) {
        *status_ret = status_$naming_directory_locked;
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

    /* 0x00E4CE32-0x00E4CE42: the slot is hash % 111 and the record base is
     * A5 + 0x400 + slot * 0x28. */
    int cache_idx = (uint32_t)hash_val % DIR_ENTRY_CACHE_SLOTS;
    dir_$entry_cache_t *cache_entry = &DIR_ENTRY_CACHE_OF(blk, cache_idx);

    /* 0x00E4CE3E-0x00E4CE4A: `and.b #0xfc` then `lsr.w #0x2`. */
    uint8_t cached_name_len =
        (uint8_t)((cache_entry->len_flags & DIR_ENTRY_CACHE_LEN_MASK)
                  >> DIR_ENTRY_CACHE_LEN_SHIFT);
    if (cached_name_len != name_len) {
        goto cache_miss;
    }

    /* 0x00E4CE54-0x00E4CE66: `cmpm.l` over the two UID longwords. */
    if (cache_entry->dir_uid.high != uid->high ||
        cache_entry->dir_uid.low != uid->low) {
        goto cache_miss;
    }

    /* Compare entry name bytes */
    if (name_len != 0) {
        int16_t remaining = name_len - 1;
        uint32_t i = 1;
        do {
            /* 0x00E4CE82-0x00E4CE8A: `(0x416,A0)` with A0 = A4 + i, i.e.
             * the 1-based name byte at record+0x17 + (i - 1). */
            if ((char)cache_entry->name[i - 1] != (char)name_bytes[i - 1]) {
                goto cache_miss;
            }
            i++;
            remaining--;
        } while (remaining != -1);
    }

    /* Compare DTV (6 bytes at cache offset 0x410) */
    {
        int16_t *p1 = (int16_t *)dtv_data;
        int16_t *p2 = (int16_t *)cache_entry->dtv;
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
    uid_ret->high = cache_entry->entry_uid.high;
    uid_ret->low  = cache_entry->entry_uid.low;
    *extra_ret = 0;
    DIR_ENTRY_CACHE_HITS_OF(blk) += 1;

    /* 0x00E4CED4-0x00E4CEDC: `btst.l #0x8` on the WORD at record+0x16 is
     * bit 0 of the len_flags BYTE. */
    if ((cache_entry->len_flags & DIR_ENTRY_CACHE_ACL_OK) != 0) {
        /* ACL already checked - done */
        DIR_ENTRY_CACHE_SKIPPED_ACL_OF(blk) += 1;
        ML_$EXCLUSION_STOP(&DIR_$MUTEX);
        return;
    }

    /* Need to check ACL rights */
    ML_$EXCLUSION_STOP(&DIR_$MUTEX);
    ACL_$RIGHTS(uid,
                (boolean *)&dir_$get_entry_ignore_super_00e4cff4,
                (uint32_t *)&dir_$get_entry_rights_00e4cff6,
                (int16_t *)&DIR_$CONST_ONE_W, status_ret);
    if (*status_ret == status_$ok) {
        return;
    }
    NAME_CONVERT_ACL_STATUS(status_ret);
    uid_ret->high = 0;  /* Clear result UID high on ACL failure */
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

    /* 0x00E4CF70-0x00E4CF78 */
    cache_entry->dir_uid.high = uid->high;
    cache_entry->dir_uid.low  = uid->low;

    /* 0x00E4CF7C-0x00E4CF84 */
    cache_entry->entry_uid.high = uid_ret->high;
    cache_entry->entry_uid.low  = uid_ret->low;

    /* 0x00E4CF88-0x00E4CF8E: a longword then a word. */
    *(uint32_t *)cache_entry->dtv = dtv_data[0];
    cache_entry->dtv[2] = *(uint16_t *)((char *)dtv_data + 4);

    /* 0x00E4CF94-0x00E4CFA0: keep the two flag bits, drop in the length. */
    cache_entry->len_flags = (uint8_t)(cache_entry->len_flags & 0x03);
    cache_entry->len_flags = (uint8_t)(((uint8_t)name_len
                                        << DIR_ENTRY_CACHE_LEN_SHIFT)
                                       | cache_entry->len_flags);

    /* 0x00E4CFA4-0x00E4CFB2: `btst.l #0x0,D2` / `sne` / `lsr.b #0x7` turns
     * bit 0 of ACL_$MIN_RIGHTS' result into a 0/1 flag in bit 0. */
    cache_entry->len_flags =
        (uint8_t)(cache_entry->len_flags & (uint8_t)~DIR_ENTRY_CACHE_ACL_OK);
    cache_entry->len_flags = (uint8_t)
        (((uint8_t)(-(int)((min_rights & 1) != 0)) >> 7)
         | cache_entry->len_flags);

    /* Copy name bytes to cache */
    if (name_len != 0) {
        int16_t remaining = name_len - 1;
        uint16_t i = 1;
        do {
            /* 0x00E4CFD2: `(0x416,A0)` with A0 = A4 + i. */
            cache_entry->name[i - 1] = name_bytes[i - 1];
            i++;
            remaining--;
        } while (remaining != -1);
    }

    ML_$EXCLUSION_STOP(&DIR_$MUTEX);
}
