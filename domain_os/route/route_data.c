#include "route/route_internal.h"

/*
 * ROUTE_$PORT_ARRAY - Array of routing port structures
 *
 * Array of 8 port structures, each 0x5C (92) bytes.
 * Total size: 8 * 92 = 736 bytes (0x2E0)
 *
 * Original address: 0xE2E0A0
 */
route_$port_t ROUTE_$PORT_ARRAY[ROUTE_$MAX_PORTS];

/*
 * ROUTE_$PORT - This node's network port
 *
 * In the original binary this is the very same longword as the network
 * field of the first port entry (0xE2E0A0 == ROUTE_$PORT_ARRAY[0].network).
 * On m68k we make the symbol an alias of the array so both names refer to
 * the same storage.
 *
 * Original address: 0xE2E0A0
 */
/*
 * Whether the two names can share storage depends on the toolchain, not on
 * the target CPU: GCC/Clang implement __attribute__((alias)) only for object
 * formats with symbol aliases (ELF), and Mach-O has no equivalent.  The test
 * is therefore on __GNUC__ && __ELF__ so that any ELF build - the m68k kernel
 * and an ELF host build of the unit tests alike - gets the real aliasing.
 *
 * On a toolchain without ELF aliases ROUTE_$PORT is a separate variable and
 * the aliasing is a documented constraint: HINT_$INIT and RIP_$INIT write
 * ROUTE_$PORT while RIP_$FIND_NEXTHOP and ROUTE_$FIND_PORT read
 * ROUTE_$PORT_ARRAY[0].network, so those writes are not visible there.  A
 * macro (#define ROUTE_$PORT ROUTE_$PORT_ARRAY[0].network) would be portable
 * but collides with hint/hint_internal.h, which defines the same name.
 */
#if defined(__GNUC__) && defined(__ELF__)
/* This is the definition of ROUTE_$PORT (an alias of ROUTE_$PORT_ARRAY);
 * GCC requires the 'extern' spelling for a variable alias. */
extern uint32_t ROUTE_$PORT __attribute__((alias("ROUTE_$PORT_ARRAY")));
#else
uint32_t ROUTE_$PORT;
#endif

/*
 * ROUTE_$SOCK_ECVAL - Socket event count value
 *
 * First 4 bytes contain a socket EC value.
 * Following 8 uint32_t pointers (at offset 0x04) point to port structures.
 *
 * Layout:
 *   +0x00: Socket EC value (4 bytes)
 *   +0x04: Pointer to port[0] structure
 *   +0x08: Pointer to port[1] structure
 *   ...
 *   +0x20: Pointer to port[7] structure
 *
 * Original address: 0xE26EE4
 */
uint32_t ROUTE_$SOCK_ECVAL;

/*
 * ROUTE_$SERVICE_MUTEX - Mutex for route service operations
 *
 * Original address: 0xE26280
 */
uint32_t ROUTE_$SERVICE_MUTEX;

/*
 * ROUTE_$CONTROL_ECVAL - Control event count value
 *
 * Original address: 0xE26F08
 */
uint32_t ROUTE_$CONTROL_ECVAL;

/*
 * ROUTE_$CONTROL_EC - Control event count
 *
 * Original address: 0xE26F0C
 */
uint32_t ROUTE_$CONTROL_EC;

/*
 * ROUTE_$SOCK - Socket reference
 *
 * Original address: 0xE26F18
 */
uint16_t ROUTE_$SOCK;

/*
 * ROUTE_$STD_N_ROUTING_PORTS - Standard number of routing ports
 *
 * Original address: 0xE26F1A
 */
int16_t ROUTE_$STD_N_ROUTING_PORTS;

/*
 * ROUTE_$N_ROUTING_PORTS - Current number of routing ports
 *
 * Original address: 0xE26F1C
 */
int16_t ROUTE_$N_ROUTING_PORTS;

/*
 * ROUTE_$ROUTING - Routing table/flag
 *
 * Original address: 0xE26F1E
 */
boolean ROUTE_$ROUTING;   /* one byte; see route/route_internal.h */

/*
 * ROUTE_$PORTP - Array of pointers to port structures
 *
 * Array of 8 pointers to route_$port_t structures, one for each
 * possible network port. Used by ROUTE_$FIND_PORT to look up
 * port info by index.
 *
 * Original address: 0xE26EE8
 */
route_$port_t *ROUTE_$PORTP[ROUTE_$MAX_PORTS];

/*
 * Wired routing area data (0xE87D68 - 0xE88228).
 *
 * On m68k these are accessed at their absolute addresses (see
 * route_internal.h); on other architectures they are plain variables.
 */
#if !defined(ARCH_M68K)
char ROUTE_$WIRED_AREA_START_SYM[1];
char ROUTE_$WIRED_AREA_END_SYM[1];
/* Contents of 0xE87D64 */
const status_$t ROUTE_$UNKNOWN_PORT_STATUS = status_$internet_unknown_network_port;
/* RIP halt packet: 16 byte header (zeros) + RIP response {cmd=2, net=-1, metric=16} */
uint8_t RIP_$HALT_PACKET[24] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0x00, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x10
};
uint16_t RTWIRED_$SEND_FLAGS;
uint32_t ROUTE_$WIRED_PAGES[ROUTE_$MAX_WIRED_PAGES];
uint32_t ROUTE_$PACKET_STATS[0x81];
uint32_t ROUTE_$STAT_OVERSIZED_STD;
uint32_t ROUTE_$STAT_DROPPED_STD_HOP;
uint32_t ROUTE_$STAT_DROPPED_STD_ROUTE;
uint32_t ROUTE_$STAT_FORWARDED_STD;
uint32_t ROUTE_$STAT_OVERSIZED_N;
uint32_t ROUTE_$STAT_DROPPED_N_HOP;
uint32_t ROUTE_$STAT_DROPPED_N_ROUTE;
uint32_t ROUTE_$STAT_FORWARDED_N;
uint32_t ROUTE_$USER_PORT_COUNT;
uint16_t ROUTE_$USER_PORT_MAX;
int16_t ROUTE_$N_WIRED_PAGES;
int16_t ROUTE_$N_USER_PORTS;
int16_t ROUTE_$NET_SERVICE_ON = 0;      /* NETWORK_OP_OR_BITS */
int16_t ROUTE_$NET_SERVICE_OFF = 1;     /* NETWORK_OP_AND_NOT_BITS */
/* Contents of 0xE878A0: 00 00 20 48.  RINGLOG_$LOGIT reads only byte 0
 * (its bit 7 is the log entry's "inbound" flag). */
uint8_t RINGLOG_$ROUTE_FORWARD[4] = { 0x00, 0x00, 0x20, 0x48 };
/* Contents of 0xE878A4 */
const status_$t ROUTE_$SOCK_EMPTY_STATUS = 0x00110006;
uint32_t RTWIRED_$CALLBACK_DATA = 0;
uint16_t ROUTE_$PROCESS_UID;
int8_t ROUTE_$CHECKSUM_ENABLED;
uint32_t ROUTE_$SERVICE_ID;
ec_$eventcount_t *PTR_ROUTE_$CONTROL_EC = (ec_$eventcount_t *)&ROUTE_$CONTROL_EC;
uint16_t ROUTE_$FWD_TIMEOUT = 1;
uint16_t ROUTE_$PACKET_SEQ = 0x8000;
uint32_t ROUTE_$LAST_UPDATE_TIME;

/*
 * ROUTE_$ANNOUNCE_TEMPLATE (0xE825E0) - the constant word 2 that
 * ROUTE_$ANNOUNCE_NET sends as its RIP template.
 */
const uint16_t ROUTE_$ANNOUNCE_TEMPLATE = 2;
#endif
