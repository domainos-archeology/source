/*
 * tpad/tpad.h - Trackpad/Pointing Device Module
 *
 * Stub header for trackpad functionality.
 * Full implementation TBD when TPAD_$ subsystem is analyzed.
 */

#ifndef TPAD_H
#define TPAD_H

#include "base/base.h"

/*
 * TPAD_$SET_UNIT - Set trackpad unit
 *
 * Associates the trackpad with a display unit.
 *
 * Parameters:
 *   unit - Display unit number
 *
 * Original address: 0x00E699DC
 */
void TPAD_$SET_UNIT(uint16_t *unit);

#endif /* TPAD_H */
