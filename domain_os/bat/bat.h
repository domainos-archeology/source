/*
 * BAT - Block Allocation Table
 *
 * This module manages disk block allocation.
 */

#ifndef BAT_H
#define BAT_H

#include "base/base.h"

/*
 * BAT_$FREE - Free disk blocks
 *
 * @param blocks   Array of block addresses to free
 * @param count    Number of blocks to free
 * @param flags    Flags controlling the operation
 * @param option   Additional option parameter
 * @param status   Output status code
 */
void BAT_$FREE(uint32_t *blocks, uint16_t count, int16_t flags,
               int16_t option, status_$t *status);

#endif /* BAT_H */
