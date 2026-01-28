/*
 * HINT - Hint Cache Management (Internal Header)
 *
 * Internal data structures and functions for the HINT subsystem.
 * This module provides caching hints for remote file operations,
 * allowing the system to quickly look up which network node holds
 * a particular file object.
 *
 * The hint file is a memory-mapped file (//node_data/hint_file) that
 * contains a hash table of hint entries indexed by the low 20 bits
 * of the file UID, modulo 64 (0x3F).
 *
 * Memory layout (m68k):
 *   - HINT globals: 0xE7DB50
 *   - Exclusion lock: 0xE2C034
 *   - Local cache: 0xE7DB50 + 0x00 to 0x17 (2 entries x 12 bytes)
 */

#ifndef HINT_INTERNAL_H
#define HINT_INTERNAL_H

#include "ast/ast.h"
#include "file/file.h"
#include "hint/hint.h"
#include "ml/ml.h"
#include "mst/mst.h"
#include "name/name.h"
#include "network/network.h"
#include "route/route.h"
#include "time/time.h"

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Hint file hash table size (64 buckets) */
#define HINT_HASH_SIZE 64 /* 0x40 */
#define HINT_HASH_MASK 0x3F

/* Slots per hash bucket */
#define HINT_SLOTS_PER_BUCKET 3

/* Size of each hash bucket in bytes */
#define HINT_BUCKET_SIZE 0x54 /* 84 bytes */

/* Size of each slot within a bucket */
#define HINT_SLOT_SIZE 0x1C /* 28 bytes */

/* Number of hint addresses per slot */
#define HINT_ADDRS_PER_SLOT 3

/* Mask for extracting the hint key from UID low word */
#define HINT_UID_MASK 0xFFFFF /* Low 20 bits */

/* Local cache constants */
#define HINT_CACHE_SIZE 2        /* Number of local cache entries */
#define HINT_CACHE_ENTRY_SIZE 12 /* Bytes per cache entry */
#define HINT_CACHE_TIMEOUT 0xF0  /* Cache entry timeout (240 clock ticks) */

/* Hint file version number for newly initialized files */
#define HINT_FILE_VERSION 7

/* Hint file header magic value indicating uninitialized */
#define HINT_FILE_UNINIT 1

/*
 * ============================================================================
 * Data Structures
 * ============================================================================
 */

/*
 * Hint address pair - network location hint
 *
 * Stores a (flags, node_id) pair indicating where a file might be located.
 */
typedef struct hint_addr_t {
  uint32_t flags;   /* 0x00: Flags/status for this hint */
  uint32_t node_id; /* 0x04: Node ID where file might be located */
} hint_addr_t;

/*
 * Hint slot - single entry within a hash bucket
 *
 * Each slot can hold hint information for one UID (masked to 20 bits).
 * Contains the UID key and up to 3 network location hints.
 *
 * Size: 28 bytes (0x1C)
 */
typedef struct hint_slot_t {
  uint32_t uid_low_masked; /* 0x00: UID low word & 0xFFFFF (key) */
  hint_addr_t addrs[3];    /* 0x04: Up to 3 hint addresses (24 bytes) */
} hint_slot_t;

/*
 * Hint bucket - hash bucket containing multiple slots
 *
 * Each bucket holds 3 slots for different UIDs that hash to the same bucket.
 *
 * Size: 84 bytes (0x54)
 */
typedef struct hint_bucket_t {
  hint_slot_t slots[HINT_SLOTS_PER_BUCKET]; /* 3 slots x 28 bytes = 84 bytes */
} hint_bucket_t;

/*
 * Hint file header
 *
 * The hint file starts with a header followed by the hash table.
 *
 * Offset 0x00: version (7 = initialized, 1 = needs init)
 * Offset 0x04: network port low word
 * Offset 0x08: network info (2 shorts from ROUTE_$PORTP + 0x2E)
 */
typedef struct hint_file_header_t {
  uint32_t version;  /* 0x00: File version (7 = initialized) */
  uint32_t net_port; /* 0x04: Network port for this node */
  uint32_t net_info; /* 0x08: Network info (2 shorts packed) */
} hint_file_header_t;

/*
 * Complete hint file structure
 *
 * The hint file contains a header followed by 64 hash buckets.
 * Total size: 12 + (64 * 84) = 5388 bytes
 */
typedef struct hint_file_t {
  hint_file_header_t header;             /* 12 bytes */
  hint_bucket_t buckets[HINT_HASH_SIZE]; /* 64 * 84 = 5376 bytes */
} hint_file_t;

/*
 * Local hint cache entry
 *
 * Small local cache to avoid repeated lookups into the hint file.
 * Cache entries expire after HINT_CACHE_TIMEOUT clock ticks.
 *
 * Size: 12 bytes (0x0C)
 */
typedef struct hint_cache_entry_t {
  uint32_t timestamp;      /* 0x00: TIME_$CLOCKH when entry was added */
  uint8_t result;          /* 0x04: Cached lookup result */
  uint8_t pad[3];          /* 0x05: Padding */
  uint32_t uid_low_masked; /* 0x08: UID key (low 20 bits) */
} hint_cache_entry_t;

/*
 * HINT subsystem global state
 *
 * Contains all global state for the HINT module.
 * Located at 0xE7DB50 on m68k.
 */
typedef struct hint_globals_t {
  hint_cache_entry_t cache[HINT_CACHE_SIZE]; /* 0x00: Local cache (24 bytes) */
  uint16_t cache_index;                      /* 0x18: Next cache slot to use */
  uint16_t bucket_index; /* 0x1A: Internal round-robin index */
  /* Additional space for internal state */
  hint_file_t *hintfile_ptr; /* 0x20: Pointer to mapped hint file */
  uid_t hintfile_uid;        /* 0x24: UID of the hint file */
} hint_globals_t;

/*
 * ============================================================================
 * Global Variable Declarations
 * ============================================================================
 */

#if defined(ARCH_M68K)

/* Base of HINT globals */
#define HINT_GLOBALS_BASE 0xE7DB50

/* Pointer to mapped hint file (at 0xE2459C) */
#define HINT_$HINTFILE_PTR (*(hint_file_t **)0xE2459C)

/* UID of the hint file (at 0xE7DB68) */
#define HINT_$HINTFILE_UID (*(uid_t *)0xE7DB68)

/* Exclusion lock for hint operations (at 0xE2C034) */
#define HINT_$EXCLUSION_LOCK (*(ml_$exclusion_t *)0xE2C034)

/* Local cache array (at 0xE7DB50) */
#define HINT_$CACHE ((hint_cache_entry_t *)0xE7DB50)

/* Cache index (at 0xE7DB74) */
#define HINT_$CACHE_INDEX (*(uint16_t *)0xE7DB74)

/* Bucket round-robin index (at 0xE7DB76) */
#define HINT_$BUCKET_INDEX (*(uint16_t *)0xE7DB76)

/* Network routing port pointer (external reference) */
#define ROUTE_$PORTP (*(uint8_t **)0xE26EE8)

/* Network port for current node */
#define ROUTE_$PORT (*(uint32_t *)0xE2E0A0)

#else
/* Non-m68k: extern declarations */
extern hint_file_t *HINT_$HINTFILE_PTR;
extern uid_t HINT_$HINTFILE_UID;
extern ml_$exclusion_t HINT_$EXCLUSION_LOCK;
extern hint_cache_entry_t HINT_$CACHE[];
extern uint16_t HINT_$CACHE_INDEX;
extern uint16_t HINT_$BUCKET_INDEX;
extern uint8_t *ROUTE_$PORTP;
extern uint32_t ROUTE_$PORT;
#endif

/*
 * ============================================================================
 * Internal Function Prototypes
 * ============================================================================
 */

/*
 * HINT_$add_internal - Add hint to the hint file (internal)
 *
 * Adds or updates a hint entry in the hint file. Called by all public
 * add functions after acquiring the exclusion lock.
 *
 * The function:
 * 1. Looks up the hash bucket for the given UID
 * 2. If UID already exists in a slot, updates/shifts the addresses
 * 3. If not found, allocates a new slot (using round-robin if all full)
 * 4. Handles self-references (when hint points to same node as UID)
 *
 * Parameters:
 *   uid_ptr   - Pointer to UID to add hint for (uses low word & 0xFFFFF)
 *   addresses - Array of hint addresses (flags, node_id pairs)
 *
 * Note: Caller must hold HINT_$EXCLUSION_LOCK.
 *
 * Original address: 0x00E49A2C
 */
void HINT_$add_internal(uid_t *uid_ptr, hint_addr_t *addresses);

/*
 * HINT_$clear_hintfile - Clear and reinitialize hint file
 *
 * Called when the hint file needs to be reinitialized (version != 7).
 * Truncates the file to zero and fills it with the initialized structure.
 *
 * Sets:
 *   - Header version to 7
 *   - Header net_port to 0
 *   - Header net_info from ROUTE_$PORTP
 *   - All bucket slots to zero
 *
 * Original address: 0x00E31194
 */
void HINT_$clear_hintfile(void);

/*
 * ============================================================================
 * Hint File Path
 * ============================================================================
 */

/* Path to the hint file (in node_data) */
#define HINT_FILE_PATH "`node_data/hint_file"
#define HINT_FILE_PATH_LEN 20

#endif /* HINT_INTERNAL_H */
