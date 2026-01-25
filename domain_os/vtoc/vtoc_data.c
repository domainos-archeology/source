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
 * Base address: 0xE784D0
 *
 * The structure contains:
 *   - Per-volume data at offsets from base
 *   - Mount status array at base + 0x277
 *   - Format flag array at base + 0x27F
 */
vtoc_$data_t vtoc_$data;

/*
 * Disk data base reference
 * Address: 0xE784D0
 *
 * This is the same as vtoc_$data but used as a byte array for
 * offset calculations.
 */
uint8_t OS_DISK_DATA[0x300];

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
 * Used by vtoc_$uid_cache_lookup (FUN_00e38324) to cache recent
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
