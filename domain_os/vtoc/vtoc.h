/*
 * VTOC - Volume Table of Contents
 *
 * This module manages volume mounting and dismounting.
 */

#ifndef VTOC_H
#define VTOC_H

#include "base/base.h"

/*
 * VTOC_$DISMOUNT - Dismount a volume
 *
 * @param vol_index  Volume index to dismount
 * @param flags      Dismount flags
 * @param status     Output status code
 */
void VTOC_$DISMOUNT(uint16_t vol_index, uint8_t flags, status_$t *status);

#endif /* VTOC_H */
