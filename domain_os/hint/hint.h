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

#endif /* HINT_H */
