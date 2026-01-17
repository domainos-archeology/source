/*
 * DIR - Directory Operations Module
 *
 * This module provides directory operations for Domain/OS.
 * This is a stub header - full implementation TBD.
 */

#ifndef DIR_H
#define DIR_H

#include "base/base.h"

/*
 * DIR_$FIND_NET - Find network node for a directory entry
 *
 * Original address: 0x00E4E8B4
 *
 * Parameters:
 *   param_1 - Unknown (0x29C = 668, possibly a table offset)
 *   index   - Index value (low 20 bits of UID)
 *
 * Returns:
 *   High part of node address
 */
extern uint32_t DIR_$FIND_NET(uint32_t param_1, uint32_t *index);

#endif /* DIR_H */
