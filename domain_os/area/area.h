/*
 * AREA - Area (Multi-Segment) Management
 *
 * This module provides area-level operations for multi-segment mappings.
 */

#ifndef AREA_H
#define AREA_H

#include "base/base.h"

/*
 * AREA_$PARTNER_PKT_SIZE - Packet size for area partner operations
 */
extern int16_t AREA_$PARTNER_PKT_SIZE;

/*
 * AREA_$PARTNER - Current area partner for network operations
 */
extern void *AREA_$PARTNER;

#endif /* AREA_H */
