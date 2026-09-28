/*
 * VTOC - Volume Table of Contents Data
 *
 * Global variables for the VTOC subsystem.
 * Original m68k addresses shown in comments.
 */

#include "vtoc/vtoc_internal.h"

/*
 * VTOC data area
 *
 * This is the main storage for per-volume VTOC information.
 * Base address: 0xE784D0 (Ghidra label OS_DISK_DATA)
 *
 * The structure contains (see vtoc_$data_t in vtoc_internal.h):
 *   - Per-volume data at offsets from base
 *   - Cache hit / lookup counters at base + 0x268 / 0x26C
 *   - Per-volume write-protect flags at base + 0x270 (1-based)
 *   - Mount status array at base + 0x277
 *   - Format flag array at base + 0x27F
 *   - Dirty flag at base + 0x286 (initially 0xFF in the image)
 *
 * OS_DISK_DATA (vtoc_internal.h) is this same object viewed as bytes.
 */
vtoc_$data_t vtoc_$data = {
    .dirty = (int8_t)0xFF,
};

/*
 * UID constants for VTOC block types
 */

/* VTOC block UID - Address: 0xE1739C */
uid_t VTOC_$UID = UID_CONST(0x00000202, 0);

/* VTOC bucket UID - Address: 0xE173AC */
uid_t VTOC_BKT_$UID = UID_CONST(0x00000204, 0);

/* Note: UID_$NIL is defined in uid/uid_data.c */

/*
 * Special UIDs for ACL defaults
 */

/* Nil user UID - Address: 0xE174EC */
uid_t PPO_$NIL_USER_UID = UID_CONST(0x00800000, 0);

/* Nil group UID - Address: 0xE17524 */
uid_t RGYC_$G_NIL_UID = UID_CONST(0x00800040, 0);

/* Nil organization UID - Address: 0xE17574 */
uid_t PPO_$NIL_ORG_UID = UID_CONST(0x00800080, 0);

/*
 * UID cache for quick VTOCE lookup
 *
 * Base address: 0xEB2C00
 * 101 buckets, 4 entries per bucket (0x40 bytes per bucket)
 *
 * Used by vtoc_$uid_cache_lookup (0x00e38324) to cache recent
 * UID-to-block mappings and avoid disk lookups.
 */
vtoc_$uid_cache_bucket_t vtoc_$uid_cache[VTOC_UID_CACHE_BUCKETS];

/*
 * Block free list for truncation
 *
 * Address: 0xE78758 (offset 0x288 from vtoc_$data base)
 * Used to accumulate blocks to free during VTOCE_$TRUNCATE.
 */
uint32_t vtoc_$free_list[64];

/*
 * VTOC_CACH_LOOKUPS (0xE7873C) and VTOC_CACH_HITS (0xE78738) live inside
 * vtoc_$data (fields cach_lookups / cach_hits).  The per-volume
 * write-protect flags that the original code addressed as
 * (&VTOC_CACH_LOOKUPS)[vol_idx + 3] are vtoc_$data.cach_wp_flag[vol_idx - 1].
 */

/*
 * 0x00E38F7E: the byte VTOC_$ALLOCATE (`pea (0x11e,PC)` at 0x00E38E5E) and
 * VTOCE_$WRITE (`pea (-0x81c,PC)` at 0x00E39798) both hand to
 * VTOCE_$NEW_TO_OLD as its by-reference flags argument.  Image bytes
 * 00 00: "do not substitute the alternate parent UID".  It sits in the
 * VTOC_ code region and is never written; it is not const only because
 * VTOCE_$NEW_TO_OLD's parameter is a plain char *.
 */
char vtoc_$new_to_old_flags_00e38f7e = 0;
