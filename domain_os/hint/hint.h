/*
 * HINT - Hint Cache Management
 *
 * Provides caching hints for remote file operations. The HINT subsystem
 * maintains a persistent hint file that maps file UIDs to network node
 * locations, speeding up remote file access by avoiding broadcast lookups.
 *
 * The subsystem has two components:
 * 1. Persistent hint file: Memory-mapped file with hash table of hints
 * 2. Local cache: Small in-memory cache for recent lookups
 */

#ifndef HINT_H
#define HINT_H

#include "base/base.h"

/*
 * ============================================================================
 * Initialization and Shutdown
 * ============================================================================
 */

/*
 * HINT_$INIT - Initialize the hint subsystem
 *
 * Opens or creates the hint file (//node_data/hint_file), maps it into
 * memory, and initializes the hint data structures if needed.
 *
 * If the hint file doesn't exist, creates it. If the version is wrong,
 * reinitializes the file contents.
 *
 * Called during system initialization.
 *
 * Original address: 0x00E3122C
 */
void HINT_$INIT(void);

/*
 * HINT_$INIT_CACHE - Initialize the local hint cache
 *
 * Initializes the exclusion lock and clears the local cache entries.
 * Called before HINT_$INIT.
 *
 * Original address: 0x00E313C8
 */
void HINT_$INIT_CACHE(void);

/*
 * HINT_$SHUTDN - Shut down the hint subsystem
 *
 * Unmaps the hint file and releases associated resources.
 * Called during system shutdown.
 *
 * Original address: 0x00E49908
 */
void HINT_$SHUTDN(void);

/*
 * ============================================================================
 * Hint Lookup Functions
 * ============================================================================
 */

/*
 * HINT_$GET_HINTS - Get hints for a remote file
 *
 * Retrieves hint information for a remote file UID. Searches the hint
 * hash table for entries matching the low 20 bits of the UID.
 *
 * The returned addresses array contains (flags, node_id) pairs,
 * terminated by an entry with the local node ID (NODE_$ME).
 *
 * Parameters:
 *   file_uid  - Pointer to file UID to look up
 *   addresses - Output buffer for hint addresses (array of uint32_t pairs)
 *
 * Returns:
 *   Number of hint entries found (including the terminating local node entry)
 *
 * Original address: 0x00E49966
 */
int16_t HINT_$GET_HINTS(uid_t *file_uid, uint32_t *addresses);

/*
 * HINT_$GET_NET - Get network port from hint file
 *
 * Retrieves the network port stored in the hint file header, if the
 * hint file is valid and the network info matches the current node.
 *
 * Parameters:
 *   port_ret - Pointer to receive the network port (or 0 if not valid)
 *
 * Original address: 0x00E49CC0
 */
void HINT_$GET_NET(uint32_t *port_ret);

/*
 * ============================================================================
 * Hint Addition Functions
 * ============================================================================
 */

/*
 * HINT_$ADDI - Add hint with inline address
 *
 * Adds hint information for a file UID. The addresses parameter is passed
 * directly (not as a pointer to be dereferenced).
 *
 * Parameters:
 *   uid_ptr   - Pointer to file UID
 *   addresses - Hint addresses (inline, not a pointer)
 *
 * Original address: 0x00E49B74
 */
void HINT_$ADDI(uid_t *uid_ptr, uint32_t *addresses);

/*
 * HINT_$ADD - Add hint with address pointer
 *
 * Adds hint information for a file UID. The address data is copied from
 * the pointed-to location.
 *
 * Parameters:
 *   uid_ptr   - Pointer to file UID
 *   addr_ptr  - Pointer to hint address data
 *
 * Original address: 0x00E49BB8
 */
void HINT_$ADD(uid_t *uid_ptr, uint32_t *addr_ptr);

/*
 * HINT_$ADDU - Add hint with UID lookup
 *
 * Looks up existing hints for source_uid, then adds those hints to
 * target_uid. Used when a file reference is followed to add hints
 * from the source to the target.
 *
 * Parameters:
 *   target_uid - Pointer to UID to add hints to
 *   source_uid - Pointer to UID to look up hints from
 *
 * Original address: 0x00E49C08
 */
void HINT_$ADDU(uid_t *target_uid, uid_t *source_uid);

/*
 * HINT_$ADD_NET - Add network port to hint file
 *
 * Stores the network port in the hint file header.
 *
 * Parameters:
 *   net_port - Network port value to store
 *
 * Original address: 0x00E49C76
 */
void HINT_$ADD_NET(uint32_t net_port);

/*
 * ============================================================================
 * Local Cache Functions
 * ============================================================================
 */

/*
 * HINT_$LOOKUP_CACHE - Look up location in local hint cache
 *
 * Checks if a UID's location is in the local cache. The local cache
 * provides faster lookups than the hint file for recently accessed UIDs.
 *
 * Cache entries expire after ~240 clock ticks.
 *
 * Parameters:
 *   uid_low_masked_ptr - Pointer to uint32_t containing (UID low & 0xFFFFF)
 *   result             - Output: cached result byte (0 if not found/expired)
 *
 * Original address: 0x00E49D06
 */
void HINT_$LOOKUP_CACHE(uint32_t *uid_low_masked_ptr, uint8_t *result);

/*
 * HINT_$ADD_CACHE - Add entry to local hint cache
 *
 * Adds a lookup result to the local cache for faster future access.
 *
 * Parameters:
 *   uid_low_masked_ptr - Pointer to uint32_t containing (UID low & 0xFFFFF)
 *   result_ptr         - Pointer to result byte to cache
 *
 * Original address: 0x00E49D88
 */
void HINT_$ADD_CACHE(uint32_t *uid_low_masked_ptr, uint8_t *result_ptr);

#endif /* HINT_H */
