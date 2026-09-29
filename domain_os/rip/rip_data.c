/*
 * RIP module data
 *
 * Module data blocks RIP_$WIRED_DATA, RIP_$INIT_DATA and RIP_$RTWIRED_DATA:
 * Claude Opus 5.5 (source-thww).
 *
 * The RIP module's three data segments (docs/design-per-process-data.md), as
 * MODULE_DATA blocks that build/sau2/layout.ld links in the SAU2 map's
 * order; the address is the ordering key, not the link address.  Layouts and
 * asserts in rip/rip.h.
 *
 *   0x00E26258  RIP_$WIRED_DATA    map "D E26258 RIP_WIRED size = C8C"
 *   0x00E3502C  RIP_$INIT_DATA     map "D E3502C RIP_WIRED size = 4"
 *   0x00E87D68  RIP_$RTWIRED_DATA  map "D E87D68 RIP_RTWIRED size = 18"
 */

#include "rip/rip_internal.h"

/*
 * RIP_$WIRED_DATA, 0x00E26258..0x00E26EE3 (`gsk read 0xE26258 3212`): every
 * byte is zero except
 *
 *   0x00E262AC  +0x054  00 01                     stats._reserved0 = 1
 *   0x00E26EBC  +0xC64  ff ff                     std_idp_channel = -1
 *   0x00E26EBE  +0xC66  00 03                     ns_announcement
 *   0x00E26EC0  +0xC68  00 90 00 02 00 01 00 01   bcast_control
 *               +0xC70  00 00 00 00 ff ff 00 00
 *               +0xC78  14 zero bytes
 *
 * The three locks are set up by RIP_$INIT (0x00E2FBDE-0x00E2FC12) and the
 * routing table and counters are filled at run time.
 */
MODULE_DATA_DEFINE_INIT(rip_$wired_data_t, RIP_$WIRED_DATA, 0x00E26258, {
    .stats = {
        ._reserved0 = 0x0001,
    },
    .std_idp_channel = -1,
    .ns_announcement = { 0x00, 0x03 },
    .bcast_control = {
        0x00, 0x90, 0x00, 0x02, 0x00, 0x01, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    },
});

/*
 * RIP_$INIT_DATA, 0x00E3502C (`gsk read 0xE3502C 4`: 00 00 00 00).
 */
MODULE_DATA_DEFINE(rip_$init_data_t, RIP_$INIT_DATA, 0x00E3502C);

/*
 * RIP_$RTWIRED_DATA, 0x00E87D68..0x00E87D7F (`gsk read 0xE87D68 24`):
 *
 *   +0x00  12 zero bytes             dest_addr
 *   +0x0C  00 00                     send_flags
 *   +0x0E  00 00
 *   +0x10  00 02 ff ff ff ff 00 10   halt_data: RIP response (command 2),
 *                                    network 0xFFFFFFFF, metric 16
 */
MODULE_DATA_DEFINE_INIT(rip_$rtwired_data_t, RIP_$RTWIRED_DATA, 0x00E87D68, {
    .halt_data = { 0x00, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x10 },
});
