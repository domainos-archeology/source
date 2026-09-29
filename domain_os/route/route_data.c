/*
 * ROUTE Data - global data of the ROUTE subsystem
 *
 * Module data blocks ROUTE_$WIRED_DATA, ROUTE_$UNWIRED_DATA and
 * ROUTE_$RTWIRED_DATA: Claude Opus 5.5 (source-ybch).
 */

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
 * ROUTE_$SERVICE_MUTEX (map 0xE26280) lies in the RIP_WIRED segment: it is
 * RIP_$WIRED_DATA.route_service_mutex (rip/rip.h, rip/rip_data.c).
 */

/*
 * =============================================================================
 * The ROUTE module data blocks (layouts and asserts in route/route.h)
 * =============================================================================
 *
 * MODULE_DATA blocks linked in the SAU2 map's order; the address is the
 * ordering key, not the link address.  Module data blocks: Claude Opus 5.5
 * (source-ybch).
 */

/*
 * ROUTE_$WIRED_DATA, map "D E26EE4 ROUTE_WIRED size = 3C"
 * (`gsk read 0xE26EE4 0x3C`):
 *
 *   +0x00  00 00 00 00                       sock_ecval
 *   +0x04  00 e2 e0 a0  00 e2 e0 fc  00 e2 e1 58  00 e2 e1 b4
 *   +0x14  00 e2 e2 10  00 e2 e2 6c  00 e2 e2 c8  00 e2 e3 24
 *                                            portp[0..7] = 0xE2E0A0 + i*0x5C
 *   +0x24  00 00 00 00                       control_ecval
 *   +0x28  12 zero bytes                     control_ec (EC_$INIT'd at run time)
 *   +0x34  ff ff                             sock = 0xFFFF (closed)
 *   +0x36  00 00 00 00 00 00                 the port counts, routing, pad
 *
 * portp[i] is the address of ROUTE_$PORT_ARRAY[i] (NET_PORT_TABLE at
 * 0xE2E0A0, stride 0x5C), a linked object, so the pointers name it.
 */
MODULE_DATA_DEFINE_INIT(route_$wired_data_t, ROUTE_$WIRED_DATA, 0x00E26EE4, {
    .portp = {
        &ROUTE_$PORT_ARRAY[0], &ROUTE_$PORT_ARRAY[1],
        &ROUTE_$PORT_ARRAY[2], &ROUTE_$PORT_ARRAY[3],
        &ROUTE_$PORT_ARRAY[4], &ROUTE_$PORT_ARRAY[5],
        &ROUTE_$PORT_ARRAY[6], &ROUTE_$PORT_ARRAY[7],
    },
    .sock = 0xFFFF,
});

/*
 * ROUTE_$UNWIRED_DATA, map "D E825DC ROUTE_UNWIRED size = 8"
 * (`gsk read 0xE825DC 8`: 00 00 00 00 00 02 00 00): start_time 0 and the
 * announce template word 2.
 */
MODULE_DATA_DEFINE_INIT(route_$unwired_data_t, ROUTE_$UNWIRED_DATA, 0x00E825DC, {
    .announce_template = 2,
});

/*
 * ROUTE_$RTWIRED_DATA, map "D E87D80 ROUTE_RTWIRED size = 4A8"
 * (`gsk read 0xE87D80 1192`): every byte zero except the last eight,
 *
 *   +0x4A0  00 e2 6f 0c  00 01  80 00
 *
 * ptr_control_ec = 0xE26F0C, the address of ROUTE_$WIRED_DATA.control_ec
 * (a linked object, so the pointer names it), fwd_timeout = 1 and
 * packet_seq = 0x8000.
 */
MODULE_DATA_DEFINE_INIT(route_$rtwired_data_t, ROUTE_$RTWIRED_DATA, 0x00E87D80, {
    .ptr_control_ec = &ROUTE_$WIRED_DATA.control_ec,
    .fwd_timeout    = 1,
    .packet_seq     = 0x8000,
});
