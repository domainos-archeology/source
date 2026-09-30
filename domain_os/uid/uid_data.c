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

#include "uid/uid_internal.h"

/*
 * UID_$NIL - The nil/empty UID
 *
 * Used to represent "no UID" or an uninitialized UID.
 * This is a constant with all zeros.
 */
uid_t UID_$NIL = UID_CONST(0, 0);

/*
 * UID generator state
 *
 * Contains the last generated UID value. The high word is based on
 * the system clock, and the low word contains the node ID and a counter.
 *
 * Original address: 0xE2C008
 */
uid_t UID_$GENERATOR_STATE = UID_CONST(0, 0);

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
uid_t PV_LABEL_$UID = UID_CONST(0x00000200, 0);

/* Logical volume label UID - Address: 0xE17394 */
uid_t LV_LABEL_$UID = UID_CONST(0x00000201, 0);

/*
 * The remaining well-known UIDs, all slots of the read-only table the SAU2 map
 * calls "I E1737C UID_LIST size = 210".  Every slot is 8 bytes, laid out
 * high-longword first; the image values below were read with
 * `gsk read 0x00E1737C 0xB8`.
 */

/* Symbolic link file type - Address: 0xE173BC */
uid_t SLINK_$UID = UID_CONST(0x0000031e, 0);

/* Unstructured file type - Address: 0xE173C4 */
uid_t UNSTRUCT_$UID = UID_CONST(0x00000321, 0);

/* Display 1 object - Address: 0xE173D4 */
uid_t DISPLAY1_$UID = UID_CONST(0x00000400, 0);

/* Diskless node pattern - Address: 0xE173F4 */
uid_t DISKLESS_$UID = UID_CONST(0x00000403, 0);

/* OS wired/pinned memory - Address: 0xE1740C */
uid_t OS_WIRED_$UID = UID_CONST(0x00000406, 0);

/* Anonymous (unnamed) object - Address: 0xE17414 */
uid_t ANON_$UID = UID_CONST(0x00000407, 0);

/* Unknown network UID - Address: 0xE174A4 (`gsk read 0xE174A4 8` =
 * 00 00 07 05 00 00 00 00); NET_IO_$DEVICE_STAT / _STAT2 answer it for a
 * port that does not exist (0x00E5A3D2, 0x00E5A456) */
uid_t UNKNOWN_$NETWORK_UID = UID_CONST(0x00000705, 0);

