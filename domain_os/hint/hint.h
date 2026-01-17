/*
 * HINT - Hint Cache Management
 *
 * Provides caching hints for remote file operations.
 */

#ifndef HINT_H
#define HINT_H

#include "base/base.h"

/*
 * HINT_$LOOKUP_CACHE - Look up location in hint cache
 *
 * Checks if a remote file location is cached locally.
 *
 * Parameters:
 *   uid_low_masked - Pointer to masked low UID (UID low & 0xFFFFF)
 *   result         - Output result byte (negative = local cache valid)
 *
 * Original address: 0x00E49D06
 */
void HINT_$LOOKUP_CACHE(void *uid_low_masked, int8_t *result);

/*
 * HINT_$GET_HINTS - Get hints for a remote file
 *
 * Retrieves hint information for a remote file UID.
 *
 * Parameters:
 *   file_uid   - File UID to get hints for
 *   hint_node  - Output buffer for hint node information
 *
 * Returns:
 *   Number of hints found
 *
 * Original address: TBD
 */
int32_t HINT_$GET_HINTS(uid_t *file_uid, void *hint_node);

/*
 * HINT_$ADD_CACHE - Add entry to hint cache
 *
 * Adds a location to the hint cache.
 *
 * Parameters:
 *   node_id    - Pointer to node identifier
 *   hint_flag  - Output hint flag
 *
 * Original address: TBD
 */
void HINT_$ADD_CACHE(void *node_id, void *hint_flag);

/*
 * HINT_$ADDI - Add hint information
 *
 * Adds hint info for a file to the cache.
 *
 * Parameters:
 *   file_uid  - File UID
 *   node_id   - Node identifier
 *
 * Original address: TBD
 */
void HINT_$ADDI(uid_t *file_uid, void *node_id);

#endif /* HINT_H */
