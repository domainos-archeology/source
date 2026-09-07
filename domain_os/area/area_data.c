/*
 * area_data.c - AREA Module Global Data Definitions
 *
 * This file defines the global variables used by the AREA (Multi-Segment
 * Area Management) module. Areas are contiguous virtual address ranges
 * that can span multiple segments, supporting dynamic growth, copy-on-write
 * duplication, and remote (networked) backing storage.
 *
 * All variables are initialized to zero/NULL here; runtime initialization
 * occurs in AREA_$INIT (0x00E2F3A8).
 *
 * Original M68K addresses:
 *   AREA_$IN_TRANS_EC:       0xE1E160 (12 bytes) - In-transition eventcount
 *   AREA_$FREE_LIST:         0xE1E6E0 (4 bytes)  - Head of free entry list
 *   AREA_$PARTNER:           0xE1E6E4 (4 bytes)  - Network partner pointer
 *   AREA_$DEL_DUP:           0xE1E6F4 (2 bytes)  - Duplicate delete count
 *   AREA_$CR_DUP:            0xE1E6F6 (2 bytes)  - Duplicate create count
 *   AREA_$N_FREE:            0xE1E6F8 (2 bytes)  - Free entry count
 *   AREA_$N_AREAS:           0xE1E6FA (2 bytes)  - Highest area ID in use
 *   AREA_$PARTNER_PKT_SIZE:  0xE1E6FC (2 bytes)  - Partner packet size
 */

#include "area/area_internal.h"

/*
 * ============================================================================
 * Eventcounts
 * ============================================================================
 */

/*
 * In-transition eventcount
 *
 * Used by area_$wait_in_trans() to wait for an area operation that
 * has AREA_FLAG_IN_TRANS set to complete before proceeding.
 *
 * Original address: 0xE1E160
 */
ec_$eventcount_t AREA_$IN_TRANS_EC = { 0 };

/*
 * ============================================================================
 * Free List Management
 * ============================================================================
 */

/*
 * Head of free area entry list
 *
 * Points to the first free area_$entry_t. Entries are linked
 * through their 'next' field. Populated by AREA_$INIT.
 *
 * Original address: 0xE1E6E0
 */
area_$entry_t *AREA_$FREE_LIST = NULL;

/*
 * Count of free area entries
 *
 * Number of entries remaining in the free list.
 * Decremented on AREA_$CREATE, incremented on AREA_$DELETE.
 *
 * Original address: 0xE1E6F8
 */
int16_t AREA_$N_FREE = 0;

/*
 * Highest area ID currently in use
 *
 * Tracks the maximum area ID that has been allocated.
 * Used for iteration bounds.
 *
 * Original address: 0xE1E6FA
 */
int16_t AREA_$N_AREAS = 0;

/*
 * ============================================================================
 * Partner / Network Operations
 * ============================================================================
 */

/*
 * Node address of the diskless partner ("mother node")
 *
 * Eight bytes: the high longword is always zero, the low longword holds
 * NETWORK_$MOTHER_NODE when NETWORK_$DISKLESS is set (AREA_$INIT,
 * 0x00E2F426-0x00E2F43C).  Always passed by address.
 *
 * Original address: 0xE1E6E4 (globals + 0x5CC)
 */
uid_t AREA_$PARTNER = { 0, 0 };

/*
 * Packet size for area partner operations
 *
 * Size of packets used in remote area synchronization.
 *
 * Original address: 0xE1E6FC
 */
int16_t AREA_$PARTNER_PKT_SIZE = 0;

/*
 * ============================================================================
 * Deduplication Statistics
 * ============================================================================
 */

/*
 * Count of duplicate area creates (dedup hits)
 *
 * Incremented by AREA_$CREATE_FROM when an existing area is found
 * for the same remote UID and caller_id, avoiding a new allocation.
 *
 * Original address: 0xE1E6F6
 */
int16_t AREA_$CR_DUP = 0;

/*
 * Count of duplicate area deletes
 *
 * Incremented by AREA_$DELETE_FROM when a delete is deduplicated
 * (i.e., reference count decremented rather than full deletion).
 *
 * Original address: 0xE1E6F4
 */
int16_t AREA_$DEL_DUP = 0;

/*
 * ============================================================================
 * UID deduplication hash table
 * ============================================================================
 */

/*
 * Remote-UID hash table used by AREA_$CREATE_FROM.
 *
 * Original address: 0xE1E56C (globals + 0x454), 11 buckets
 */
area_$uid_hash_t *AREA_$UID_HASH[AREA_UID_HASH_BUCKETS] = { NULL };

/*
 * Storage for the 11 hash-chain records threaded onto AREA_$UID_HASH_FREE
 * by AREA_$INIT (0x00E2F3FC-0x00E2F40A).
 *
 * Original address: 0xE1E598 (globals + 0x480)
 */
area_$uid_hash_t AREA_$UID_HASH_POOL[AREA_UID_HASH_BUCKETS] = { { NULL, NULL } };

/*
 * Head of the free list of UID hash-chain records.
 *
 * Original address: 0xE1E568 (globals + 0x450)
 */
area_$uid_hash_t *AREA_$UID_HASH_FREE = NULL;

/*
 * ============================================================================
 * Per-ASID area lists and identifier counter
 * ============================================================================
 */

/*
 * Per-ASID area list heads.  AREA_$INIT clears all 58 entries.
 *
 * Original address: 0xE1E5F0 (globals + 0x4D8)
 */
area_$entry_t *AREA_$ASID_LIST[AREA_MAX_ENTRIES] = { NULL };

/*
 * Monotonic counter handed out as area_$entry_t.caller_id by
 * area_$internal_create (0x00E078A6).  Cleared by AREA_$INIT.
 *
 * Original address: 0xE1E6DC (globals + 0x5C4)
 */
uint32_t AREA_$NEXT_CALLER_ID = 0;

/*
 * ============================================================================
 * In-code status constant
 * ============================================================================
 */

/*
 * Area_Internal_Error - the status AREA_ hands CRASH_SYSTEM when it finds its
 * own tables inconsistent.
 *
 * The cell is a literal inside the AREA_ code segment (it sits between
 * AREA_$DELETE_FROM at 0xE07D06 and AREA_$FREE_ASID at 0xE07E80, so the map
 * gives it no symbol of its own); thirteen `pea (d,PC)` sites in AREA_ point
 * at it.  Image bytes at 0x00E07E7C: 00 32 00 0a.
 *
 * Original address: 0xE07E7C
 */
status_$t Area_Internal_Error = 0x0032000A;
