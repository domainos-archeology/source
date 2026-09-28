/*
 * VTOC UID cache and UID hashing
 *
 * Three module-local routines of VTOC_ (SAU2 map: the VTOC_ code segment
 * starts at E382A8 with no interior symbol before VTOC_$ALLOCATE):
 *   vtoc_$uid_cache_insert  0x00E382A8  124 bytes (0x00E382A8 .. 0x00E38323)
 *   vtoc_$uid_cache_lookup  0x00E38324  140 bytes (0x00E38324 .. 0x00E383AF)
 *   vtoc_$hash_uid          0x00E383B0  276 bytes (0x00E383B0 .. 0x00E384C3)
 *
 * The cache at 0xEB2C00 has 101 buckets of four 16-byte entries
 * (vtoc_$uid_cache_entry_t); the bucket is (uid.high & 0xFFFF) mod 101
 * (`move.w (0x2,A1),D0w` / `divu.w #0x65` / `swap`) and the bucket's byte
 * offset is bucket << 6.
 *
 * Re-emitted from the disassembly 2026-09-19.  The previous C had the
 * entry's word fields swapped (+0x0C is the volume / valid word, +0x0E the
 * age), aged the non-matching entries with the wrong bound, made the
 * lookup's `update` argument refresh rather than remove, and let
 * vtoc_$hash_uid leave `hash` uninitialised for a hash_type above 3 and
 * `*block` unset on the new-format path.
 */

#include "vtoc/vtoc_internal.h"

/*
 * vtoc_$uid_cache_insert - remember where a UID's VTOCE lives
 *
 * Frame (link.w A6,-0x14; D2/D3/A2-A4 saved):
 *   (0x8,A6)   uid         pointer -> A1
 *   (0xc,A6)   vol_idx     word -> D1w, stored in the entry's +0x0C word
 *   (0xe,A6)   block_info  longword, stored at +0x08
 *
 * Scans the bucket's four entries: a valid entry (+0x0C non-zero) holding
 * the same UID means nothing to do; otherwise the entry with the largest
 * age (`cmp.w (0xe,A0),D0w` / `bcc` - strictly larger than the running
 * maximum, which starts at 0 with entry 0 as the default victim) is
 * overwritten with the new UID, location and volume, age 0.
 */
void vtoc_$uid_cache_insert(uid_t *uid, int16_t vol_idx, uint32_t block_info)
{
    uint16_t bucket_idx;                /* D0w after the divu / swap */
    uint16_t max_age;                   /* D0w, cleared at 0x00E382CE */
    int16_t i;                          /* D2w, dbf counter */
    vtoc_$uid_cache_entry_t *entry;     /* A0 */
    vtoc_$uid_cache_entry_t *victim;    /* A2 */

    /* 0x00E382B4 .. 0x00E382D2: bucket = (uid.high & 0xFFFF) % 101 */
    bucket_idx = (uint16_t)((uid->high & 0xFFFFu) % VTOC_UID_CACHE_BUCKETS);
    entry = vtoc_$uid_cache[bucket_idx].entries;

    /* 0x00E382D6 .. 0x00E382D8: four entries, victim = entry 0, max_age = 0 */
    max_age = 0;
    victim = entry;

    for (i = 3; i != -1; i--) {
        /* 0x00E382DC .. 0x00E382EE: same UID and +0x0C non-zero -> done */
        if (entry->uid.high == uid->high &&
            entry->uid.low == uid->low &&
            entry->vol != 0) {
            return;                                 /* 0x00E382EE -> 0x00E3831A */
        }

        /* 0x00E382F0 .. 0x00E382F8: `cmp.w (0xe,A0),D0w` / `bcc.b` - an
         * age strictly above the running maximum makes this the victim */
        if (entry->age > max_age) {
            victim = entry;
            max_age = entry->age;
        }

        entry++;                                    /* 0x00E382FC lea (0x10,A0) */
    }

    /* 0x00E38304 .. 0x00E38316 */
    victim->uid.high = uid->high;
    victim->uid.low = uid->low;
    victim->block_info = block_info;
    victim->vol = (uint16_t)vol_idx;
    victim->age = 0;
}

/*
 * vtoc_$uid_cache_lookup - find a UID in the cache
 *
 * Frame (link.w A6,-0x14; D2-D4/A2-A4 saved):
 *   (0x8,A6)   uid         pointer -> A1
 *   (0xc,A6)   flags       pointer to a word -> A2; receives the entry's
 *                          +0x0C word (the volume) on a hit
 *   (0x10,A6)  block_info  pointer -> D3; receives +0x08 on a hit
 *   (0x14,A6)  remove      byte (high half of its word slot) -> D1b;
 *                          negative removes the entry after reading it
 *
 * Every entry of the bucket is visited (`dbf D0w` from 3).  A valid entry
 * with the matching UID sets the result true and either empties it
 * (`move.l #0xffff,(0xc,A0)`: vol 0, age 0xFFFF) or resets its age to 0,
 * then copies its volume word and location out; every other entry's age
 * is incremented unless it is already 0xFFFE or more.
 *
 * Returns the Domain boolean in D0b.
 */
uint8_t vtoc_$uid_cache_lookup(uid_t *uid, uint16_t *flags, uint32_t *block_info, char remove)
{
    uint16_t bucket_idx;                /* D0w after the divu / swap */
    int16_t i;                          /* D0w, dbf counter */
    vtoc_$uid_cache_entry_t *entry;     /* A0 */
    uint8_t found;                      /* D2b */

    /* 0x00E3833C .. 0x00E38358 */
    found = 0;
    bucket_idx = (uint16_t)((uid->high & 0xFFFFu) % VTOC_UID_CACHE_BUCKETS);
    entry = vtoc_$uid_cache[bucket_idx].entries;

    for (i = 3; i != -1; i--) {
        /* 0x00E3835C .. 0x00E3836E */
        if (entry->uid.high == uid->high &&
            entry->uid.low == uid->low &&
            entry->vol != 0) {
            found = 0xFF;                           /* 0x00E38370 st D2b */

            /* 0x00E38372 tst.b D1b / bpl.b 0x00e38380 */
            if ((int8_t)remove < 0) {
                /* 0x00E38376 move.l #0xffff,(0xc,A0): vol = 0, age = 0xFFFF */
                entry->vol = 0;
                entry->age = 0xFFFF;
            } else {
                entry->age = 0;                     /* 0x00E38380 clr.w (0xe,A0) */
            }

            /* 0x00E38384 .. 0x00E3838A */
            *flags = entry->vol;
            *block_info = entry->block_info;
        } else {
            /* 0x00E38390 `cmpi.w #-0x2,(0xe,A0)` / `bcc.b`: age below 0xFFFE
             * is incremented */
            if (entry->age < 0xFFFE) {
                entry->age++;
            }
        }

        entry++;                                    /* 0x00E3839C lea (0x10,A0) */
    }

    return found;                                   /* 0x00E383A4 move.b D2b,D0b */
}

/*
 * vtoc_$hash_uid - hash a UID to its VTOC bucket and disk block
 *
 * Frame (link.w A6,-0x14; D2-D5/A2-A4 saved; A5 = 0xE784D0 inherited from
 * the caller, every caller having set it):
 *   (0x8,A6)   uid         pointer -> A2
 *   (0xc,A6)   vol_idx     word -> D2w; A3 = A5 + vol_idx*100
 *   (0xe,A6)   bucket_idx  pointer to a word; written only on the
 *                          new-format path (0x00E38458)
 *   (0x12,A6)  block       pointer -> A4
 *   (0x16,A6)  status_ret  pointer -> D3, cleared first (0x00E383CA)
 *
 * The hash (D1w) depends on the per-volume hash_type word at -0x54:
 *   2        (uid.high*2 + (uid.low < 0)) folded 16 xor 16, mod hash_size
 *   0 or 1   UID_$HASH(uid, &hash_size)
 *   3        uid.high folded 16 xor 16, mod hash_size
 *   other    D1 is left as whatever the register held (not reproduced:
 *            modelled as 0, see the TODO)
 * On a new-format volume the low two bits are the bucket and the rest the
 * bucket-block index; on an old-format one the hash is the VTOC-block
 * index.  The index is then resolved through the ten (new) or eight (old)
 * 6-byte partition entries exactly as VTOC_$GET_UID does.  A resulting
 * block of 0 is "uid mismatch" (0x80020002).
 */
void vtoc_$hash_uid(uid_t *uid, short vol_idx, uint16_t *bucket_idx,
                    uint32_t *block, status_$t *status_ret)
{
    vtoc_$vol_t *vol;                   /* A3 - 0x54 */
    uint32_t folded;                    /* D0 / D1 */
    uint16_t hash;                      /* D1w */
    uint16_t idx;                       /* D1w after the bucket split */
    int16_t part;                       /* D0w */
    int16_t i;                          /* D2w, dbf counter */

    /* 0x00E383C8 clr.l (A0): status = ok */
    *status_ret = status_$ok;

    /* 0x00E383CC .. 0x00E383D0 */
    vol = VTOC_VOL(vol_idx);

    /*
     * TODO(source-lryi): a hash_type above 3 leaves D1w with the
     * value UID_$HASH or the mulu left in it, which cannot be modelled;
     * the C uses 0.  Every volume label the kernel writes uses 0..3.
     */
    hash = 0;

    /* 0x00E383D4 cmpi.w #0x2,(-0x54,A3) / bne.b 0x00e38406 */
    if (vol->hash_type == 2) {
        /* 0x00E383DC .. 0x00E38402: D0 = uid.high * 2 + (uid.low < 0);
         * D1 = (D0 & 0xFFFF) ^ (D0 >> 16), then `divu.w (-0x52,A3)` /
         * `swap` = the remainder */
        folded = uid->high * 2u;
        if ((int32_t)uid->low < 0) {
            folded++;
        }
        folded = ((folded & 0xFFFFu) ^ (folded >> 16)) & 0xFFFFu;
        hash = (uint16_t)(folded % vol->hash_size);
    } else if (vol->hash_type < 2) {
        /* 0x00E38406 `cmpi.w #0x2` / `bcc.b`: 0 or 1 -> UID_$HASH(uid,
         * &hash_size), the low word of its result (0x00E3841C) */
        hash = (uint16_t)UID_$HASH(uid, &vol->hash_size);
    } else if (vol->hash_type == 3) {
        /* 0x00E38420 .. 0x00E38444: uid.high folded, mod hash_size */
        folded = uid->high;
        folded = ((folded & 0xFFFFu) ^ (folded >> 16)) & 0xFFFFu;
        hash = (uint16_t)(folded % vol->hash_size);
    }

    /* 0x00E38446 .. 0x00E3844E: `tst.b (0x27f,A0)` / `bpl.b 0x00e38492` */
    if (vtoc_$data.format[vol_idx] < 0) {
        /* 0x00E38450 .. 0x00E3845C: *bucket_idx = hash & 3, idx = hash >> 2,
         * *block = 0 */
        *bucket_idx = (uint16_t)(hash & 3);
        idx = (uint16_t)(hash >> 2);
        *block = 0;

        /* 0x00E3845E .. 0x00E3848C: `moveq #0x9,D2` / dbf - ten partitions */
        part = 0;
        for (i = 9; i != -1; i--) {
            if (idx < vol->parts[part].count) {                 /* 0x00E38464 bcc */
                /* 0x00E3846C .. 0x00E38480: *block = base + idx */
                *block = vol->parts[part].base + (uint32_t)idx;
                goto check;                                     /* 0x00E38482 -> 0x00E384B0 */
            }
            idx = (uint16_t)(idx - vol->parts[part].count);     /* 0x00E38484 */
            part++;
        }
    } else {
        /* 0x00E38492 .. 0x00E384AA: *block = 0; `moveq #0x7,D2` / dbf -
         * eight partitions, sharing the hit code at 0x00E3846C */
        *block = 0;
        idx = hash;
        part = 0;
        for (i = 7; i != -1; i--) {
            if (idx < vol->parts[part].count) {                 /* 0x00E3849C bcs */
                *block = vol->parts[part].base + (uint32_t)idx;
                goto check;
            }
            idx = (uint16_t)(idx - vol->parts[part].count);     /* 0x00E384A2 */
            part++;
        }
    }

    /* 0x00E384AE tst.l (A4) */
check:
    /* 0x00E384B0 bne.b / 0x00E384B2 .. 0x00E384B4: block 0 -> uid mismatch */
    if (*block == 0) {
        *status_ret = status_$VTOC_uid_mismatch;
    }
}
