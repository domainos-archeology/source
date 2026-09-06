/*
 * ASKNODE - Data Definitions
 *
 * Global data used by the ASKNODE subsystem.
 */

#include "asknode/asknode_internal.h"

/*
 * PKT_$DEFAULT_INFO - Default packet info template (0x00E82408)
 *
 * Template copied when building network requests. Contains:
 * - Packet size limits
 * - Default protocol flags
 * - Version information
 */
uint32_t PKT_$DEFAULT_INFO[8] = {
    0x00100002,  /* 0x00: size/type info */
    0x00028031,  /* 0x04: flags */
    0xFFFF0000,  /* 0x08: masks */
    0xFFFF0000,  /* 0x0C: masks */
    0x00000000,  /* 0x10: reserved */
    0x00000000,  /* 0x14: reserved */
    0x00000000,  /* 0x18: reserved */
    0x00000003   /* 0x1C: version (protocol 3) */
};

/*
 * The socket event count pointers (0x00E28DB0 + n * 4) are entries of the
 * socket pointer table in sock/sock_data.c (SOCK_$EVENT_COUNTERS).
 */

/*
 * ASKNODE_$PROTOCOL_VERSION - Protocol version word (0x00E82426, value 3)
 *
 * When == 3, WHO requests use protocol version 2; otherwise version 3.
 */
uint16_t ASKNODE_$PROTOCOL_VERSION = 3;

/*
 * ASKNODE_$EMPTY_DATA - Zero longword used as "no data" (0x00E658CC)
 */
uint32_t ASKNODE_$EMPTY_DATA = 0;

/* NETWORK_$CAPABLE_FLAGS (0xE24C3F) is defined in network/network_data.c */
