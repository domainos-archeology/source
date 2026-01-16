/*
 * VTOCE - Volume Table of Contents Entry Management
 *
 * Provides conversion between old and new attribute formats.
 */

#ifndef VTOCE_H
#define VTOCE_H

#include "base/base.h"

/*
 * VTOCE_$NEW_TO_OLD - Convert new format attributes to old format
 *
 * Converts the new 144-byte attribute format to the old format
 * for backward compatibility.
 *
 * Parameters:
 *   new_attrs  - Pointer to new format attributes (144 bytes)
 *   unused     - Unused parameter
 *   old_attrs  - Output buffer for old format attributes
 *
 * Original address: 0x00E384C4
 */
void VTOCE_$NEW_TO_OLD(void *new_attrs, void *unused, void *old_attrs);

#endif /* VTOCE_H */
