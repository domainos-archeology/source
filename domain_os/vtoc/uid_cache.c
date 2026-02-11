/*
 * VTOC UID Cache Functions
 *
 * Implements the UID-to-block cache for quick VTOCE lookup.
 * The cache is organized as 101 buckets with 4 entries each.
 * Each entry is 16 bytes: UID (8), block_info (4), age (2), valid (2).
 *
 * Original addresses:
 *   vtoc_$uid_cache_insert: 0x00e382a8
 *   vtoc_$uid_cache_lookup: 0x00e38324
 *   vtoc_$hash_uid:         0x00e383b0
 */

#include "vtoc/vtoc_internal.h"

/*
 * vtoc_$uid_cache_insert - Insert a UID-to-block mapping into the cache
 *
 * Inserts a new cache entry, replacing the oldest entry if needed.
 * If the UID already exists in the cache, the function returns without
 * updating (to preserve the existing valid entry).
 *
 * Parameters:
 *   uid        - UID to cache
 *   vol_idx    - Volume index (unused in insert, but part of signature)
 *   block_info - Block location info (block << 4 | entry)
 *
 * Original address: 0x00e382a8
 * Size: 124 bytes
 */
void vtoc_$uid_cache_insert(uid_t *uid, int16_t vol_idx, uint32_t block_info)
{
    uint16_t bucket_idx;
    int16_t i;
    vtoc_$uid_cache_entry_t *entry;
    vtoc_$uid_cache_entry_t *oldest_entry;
    uint16_t max_age;

    /* Calculate bucket from UID low word */
    bucket_idx = (uid->low & 0xFFFF) % VTOC_UID_CACHE_BUCKETS;
    entry = vtoc_$uid_cache[bucket_idx].entries;

    max_age = 0;
    oldest_entry = entry;

    /* Scan all 4 entries in the bucket */
    for (i = 3; i >= 0; i--) {
        /* Check if this UID is already in the cache */
        if (entry->uid.high == uid->high &&
            entry->uid.low == uid->low &&
            entry->valid != 0) {
            /* Already cached, don't replace */
            return;
        }

        /* Track oldest entry for replacement */
        if (entry->age > max_age) {
            max_age = entry->age;
            oldest_entry = entry;
        }

        entry++;
    }

    /* Insert into oldest entry slot */
    oldest_entry->uid.high = uid->high;
    oldest_entry->uid.low = uid->low;
    oldest_entry->block_info = block_info;
    oldest_entry->valid = (uint16_t)vol_idx;  /* Store vol_idx as valid marker */
    oldest_entry->age = 0;
}

/*
 * vtoc_$uid_cache_lookup - Look up a UID in the cache
 *
 * Searches the cache for a matching UID. On hit, returns the cached
 * block_info and optionally invalidates or refreshes the entry.
 *
 * Parameters:
 *   uid        - UID to look up
 *   flags      - Receives the valid/flags field from cache entry
 *   block_info - Receives the block location info on hit
 *   update     - If negative, refresh entry (set valid=0xFFFF);
 *                If non-negative, invalidate entry (set age=0)
 *
 * Returns:
 *   0xFF if found, 0 if not found
 *
 * Original address: 0x00e38324
 * Size: 140 bytes
 */
uint8_t vtoc_$uid_cache_lookup(uid_t *uid, uint16_t *flags, uint32_t *block_info, char update)
{
    uint16_t bucket_idx;
    int16_t i;
    vtoc_$uid_cache_entry_t *entry;
    uint8_t found = 0;

    /* Calculate bucket from UID low word */
    bucket_idx = (uid->low & 0xFFFF) % VTOC_UID_CACHE_BUCKETS;
    entry = vtoc_$uid_cache[bucket_idx].entries;

    /* Scan all 4 entries in the bucket */
    for (i = 3; i >= 0; i--) {
        if (entry->uid.high == uid->high &&
            entry->uid.low == uid->low &&
            entry->valid != 0) {
            /* Found it */
            found = 0xFF;

            if (update < 0) {
                /* Refresh: mark as recently used */
                entry->valid = 0xFFFF;
            } else {
                /* Invalidate: reset age counter */
                entry->age = 0;
            }

            *flags = entry->valid;
            *block_info = entry->block_info;
        } else {
            /* Age other entries (for LRU replacement) */
            if (entry->age < 0xFFFE) {
                entry->age++;
            }
        }

        entry++;
    }

    return found;
}

/*
 * vtoc_$hash_uid - Hash a UID to find the VTOC bucket
 *
 * Computes a hash bucket index from a UID and then searches the
 * partition table to find the actual disk block.
 *
 * Parameters:
 *   uid        - UID to hash
 *   vol_idx    - Volume index
 *   bucket_idx - Receives the bucket index (hash & 3)
 *   block      - Receives the disk block number
 *   status_ret - Receives status code
 *
 * Original address: 0x00e383b0
 * Size: 276 bytes
 */
void vtoc_$hash_uid(uid_t *uid, short vol_idx, uint16_t *bucket_idx,
                    uint32_t *block, status_$t *status_ret)
{
    int32_t vol_offset;
    uint32_t hash;
    int16_t hash_type;
    uint16_t hash_size;
    int16_t partition;
    uint16_t offset;
    uint8_t *vol_data;

    *status_ret = status_$ok;

    /* Get pointer to per-volume data */
    vol_data = OS_DISK_DATA + (vol_idx * 100);
    vol_offset = (int32_t)(uintptr_t)vol_data;

    /* Get hash parameters */
    hash_type = *(int16_t *)(vol_data - 0x54);
    hash_size = *(uint16_t *)(vol_data - 0x52);

    /* Compute hash based on hash type */
    if (hash_type == 2) {
        /* Shift-XOR hash */
        hash = uid->high * 2;
        if ((int32_t)uid->low < 0) {
            hash++;
        }
        hash = ((hash >> 16) ^ (hash & 0xFFFF)) % hash_size;
    } else if (hash_type < 2) {
        /* UID_$HASH function */
        hash = UID_$HASH(uid, (uint16_t *)(vol_data - 0x52));
    } else if (hash_type == 3) {
        /* Simple XOR hash */
        hash = ((uid->high >> 16) ^ (uid->high & 0xFFFF)) % hash_size;
    }

    /* Check volume format (new vs old) */
    if (vtoc_$data.format[vol_idx] < 0) {
        /* New format: hash points to bucket in partition */
        *bucket_idx = hash & 3;
        offset = hash >> 2;
        partition = 0;

        /* Search partitions for the one containing this offset */
        for (int16_t i = 9; i >= 0; i--) {
            uint16_t part_entries = *(uint16_t *)(vol_data + partition * 6 - 0x3C);
            if (offset < part_entries) {
                *block = *(uint32_t *)(vol_data + partition * 6 - 0x3A) + offset;
                return;
            }
            offset -= part_entries;
            partition++;
        }
    } else {
        /* Old format */
        *block = 0;
        offset = hash;
        partition = 0;

        for (int16_t i = 7; i >= 0; i--) {
            uint16_t part_entries = *(uint16_t *)(vol_data + partition * 6 - 0x3C);
            if (offset < part_entries) {
                *block = *(uint32_t *)(vol_data + partition * 6 - 0x3A) + offset;
                return;
            }
            offset -= part_entries;
            partition++;
        }
    }

    /* Not found in any partition */
    if (*block == 0) {
        *status_ret = status_$VTOC_uid_mismatch;
    }
}
