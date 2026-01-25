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
 * sock_spinlock - Socket spinlock/EC array base (0x00E28DB0)
 *
 * NOTE: Despite the name, this is also used as a socket event count
 * array base in asknode code paths (indexed as &sock_spinlock + sock_num * 4).
 * The naming/purpose confusion needs further investigation.
 *
 * Runtime-initialized by SOCK_$INIT.
 */
ec_$eventcount_t *sock_spinlock = NULL;

/*
 * SOCK_$EC_5 - Socket 5 event count (0x00E28DC4)
 *
 * Runtime-initialized by SOCK_$INIT.
 */
ec_$eventcount_t *SOCK_$EC_5 = NULL;

/*
 * NETWORK_$CAPABLE_FLAGS - Network capability flags (0x00E24C3F)
 *
 * Bit 0: Network is capable/enabled
 */
uint8_t NETWORK_$CAPABLE_FLAGS = 0;
