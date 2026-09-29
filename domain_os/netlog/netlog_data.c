/*
 * NETLOG Data - Global Variables
 *
 * Module data block NETLOG_$DATA and the NETLOG_EC image contents:
 * Claude Opus 5.5 (source-iq58).
 *
 * The NETLOG module's data (docs/design-per-process-data.md):
 *
 *   0x00E248E0  NETLOG_ASM segment, map "D E248E0 NETLOG_ASM size = 1C":
 *               the six exported cells below, each addressed absolutely by
 *               its own name (e.g. "move.l (A0),(0x00e248f4).l" at
 *               0x00E71A38).  No routine bases A5 on the segment, so the
 *               cells stay individual objects (their link order: bead
 *               source-91vs).
 *   0x00E85684  NETLOG_$DATA, map "D E85684 NETLOG size = 84": the A5 block
 *               of NETLOG_$CNTL / LOG_IT / SEND_PAGE (netlog_internal.h),
 *               a MODULE_DATA block linked in the map's order.
 */

#include "netlog/netlog_internal.h"

/*
 * Logging control flags
 *
 * NETLOG_$OK_TO_LOG: Set to -1 (0xFF) when general logging is enabled
 * NETLOG_$OK_TO_LOG_SERVER: Set to -1 when server logging is enabled
 *
 * Original addresses 0xE248E0 / 0xE248E2; zero in the image.
 */
int8_t NETLOG_$OK_TO_LOG = 0;
int8_t NETLOG_$OK_TO_LOG_SERVER = 0;

/*
 * Bitmask of enabled log kinds (0xE248E4, zero in the image)
 *
 * Each bit (0-31) corresponds to a log category. When a bit is set,
 * events of that kind will be logged.
 *
 * Bits 20 and 21 are special: they control server-side logging.
 */
uint32_t NETLOG_$KINDS = 0;

/*
 * NETLOG_$EC - page-ready eventcount (0xE248E8, 0x0C bytes).
 *
 * Advanced when a buffer page fills up and is ready to send.  The image
 * value is the EC_$INIT'd state (`gsk read 0xE248E8 12`:
 * 00 00 00 00 00 e2 48 e8 00 e2 48 e8): value 0 and an empty circular
 * waiter list whose head and tail both point back at the eventcount.
 */
ec_$eventcount_t NETLOG_$EC = {
    .value = 0,
    .waiter_list_head = (ec_$eventcount_waiter_t *)&NETLOG_$EC,
    .waiter_list_tail = (ec_$eventcount_waiter_t *)&NETLOG_$EC,
};

/*
 * Target logging server (0xE248F4 / 0xE248F8, zero in the image)
 *
 * NETLOG_$NODE: Network node ID of the logging server
 * NETLOG_$SOCK: Socket number on the logging server
 */
uint32_t NETLOG_$NODE = 0;
uint16_t NETLOG_$SOCK = 0;

/*
 * ============================================================================
 * NETLOG_$DATA - the NETLOG module block, 0x00E85684..0x00E85707
 * ============================================================================
 *
 * Layout, biases and asserts in netlog/netlog_internal.h.  The map places
 * it after the AUDIT_$*_EU entries (AUDIT_$DATA_END = 0xE85684) and before
 * DXM_WIRED_ (0xE85708).  Image contents (`gsk read 0xE85684 0x84`): the
 * first 0x10 bytes of the packet info template,
 *
 *   00 04 00 02 00 02 80 31  00 01 00 00 ff ff 00 00
 *
 * i.e. flags 0x0004, routing_type 2 (internet), addr_type 2, protocol
 * 0x8031, retry_limit 1, field_0a 0, field_0c 0xFFFF; every other byte of the
 * block is zero.
 */
MODULE_DATA_DEFINE_INIT(netlog_$data_t, NETLOG_$DATA, 0x00E85684, {
    .pkt_info = {
        .flags        = 0x0004,
        .routing_type = 2,
        .addr_type    = 2,
        .protocol     = 0x8031,
        .retry_limit  = 1,
        .field_0a     = 0,
        .field_0c     = 0xFFFF,
    },
});
