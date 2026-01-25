/*
 * uid_data.c - UID Module Global Data Definitions
 *
 * This file defines the global variables used by the UID module.
 *
 * Original M68K addresses:
 *   UID_$GENERATOR_STATE: 0xE2C008 (8 bytes)
 *   UID_$GENERATOR_LOCK:  0xE2C010 (2 bytes)
 *   NODE_$ME:             0xE245A4 (4 bytes)
 */

#include "uid.h"

/*
 * UID_$NIL - The nil/empty UID
 *
 * Used to represent "no UID" or an uninitialized UID.
 * This is a constant with all zeros.
 */
uid_t UID_$NIL = { 0, 0 };

/*
 * UID generator state
 *
 * Contains the last generated UID value. The high word is based on
 * the system clock, and the low word contains the node ID and a counter.
 *
 * Original address: 0xE2C008
 */
uid_t UID_$GENERATOR_STATE = { 0, 0 };

/*
 * UID generator spin lock
 *
 * Protects the generator state during concurrent UID generation.
 *
 * Original address: 0xE2C010
 */
uint16_t UID_$GENERATOR_LOCK = 0;

/*
 * Node identifier
 *
 * The unique identifier for this node in the network.
 * Set during system initialization.
 *
 * Original address: 0xE245A4
 */
uint32_t NODE_$ME = 0;

/*
 * Well-known system UIDs
 */

/* Physical volume label UID - Address: 0xE1738C */
uid_t PV_LABEL_$UID = {0x00000200, 0};

/* Logical volume label UID - Address: 0xE17394 */
uid_t LV_LABEL_$UID = {0x00000201, 0};
