/*
 * mac_os/mac_os_data.c - MAC_OS module data
 *
 * MAC_OS_$DATA is the module data block at 0x00E22990 (the A5 base every
 * MAC_ and MAC_OS_ routine loads with `lea (0xe22990).l,A5`).  The three
 * objects defined here are the parts of it that MAC_$OPEN, MAC_$CLOSE,
 * MAC_$DEMUX and MAC_OS_$INIT address by A5 displacement:
 *
 *   +0x7A0  MAC_OS_$CHANNEL_TABLE  10 x 20 bytes  (0x00E23130..0x00E231F8)
 *   +0x868  MAC_OS_$EXCLUSION      ml_$exclusion_t (0x00E231F8, labelled
 *                                  MAC_OS_$EXCLUSION in the image)
 *
 * The channel table base is fixed by two accesses that use different fields
 * of the same entry, both relative to A5 + channel * 20:
 *
 *   0x00E0BA26  move.w D3w,(0x7a8,A0)          -> .socket, field offset 0x08
 *   0x00E0BA30  andi.b #-0x2,(0x7b2,A0)        -> .flags,  field offset 0x12
 *
 * 0x7A8 - 0x08 = 0x7A0 = 0x7B2 - 0x12, so the table starts at A5+0x7A0 and
 * the ten entries MAC_$CLOSE's bound allows (`cmpi.w #0xa,(A2)` / `bcc` at
 * 0x00E0BA90) end at A5+0x868 -- exactly where MAC_OS_$EXCLUSION begins.
 *
 *   +0x000  MAC_OS_$PORT_PKT_TABLES  8 x 0xF4 bytes (0x00E22990..0x00E23130)
 *   +0x7A0  MAC_OS_$CHANNEL_TABLE   10 x 20 bytes  (0x00E23130..0x00E231F8)
 *   +0x868  MAC_OS_$EXCLUSION       ml_$exclusion_t (0x00E231F8, labelled
 *                                   MAC_OS_$EXCLUSION in the image)
 *   +0x87C  MAC_OS_$PORTP_TABLE      8 pointers    (0x00E2320C)
 *   +0x89C  MAC_OS_$PORT_TABLE       8 x 8 bytes   (0x00E2322C)
 *
 * The last two names come from the SAU2 map, which lists both inside
 * `D E22990 MAC_OS size = 8EC`.
 */

#include "mac_os/mac_os_internal.h"

/*
 * MAC_OS_$CHANNEL_TABLE - per-channel receive state
 *
 * Address: 0x00E23130 (MAC_OS_$DATA + 0x7A0)
 *
 * Eleven slots, not ten: MAC_OS_$OPEN's scan reads slot 10 before it decides
 * the table is full (see MAC_OS_CHANNEL_TABLE_SLOTS in mac_os/mac_os.h).  In
 * the image that slot's storage is the head of MAC_OS_$EXCLUSION; here it is
 * a sentinel of its own, which keeps the read in bounds without changing what
 * the ten real slots contain.
 */
mac_os_$channel_t MAC_OS_$CHANNEL_TABLE[MAC_OS_CHANNEL_TABLE_SLOTS];

/*
 * MAC_OS_$EXCLUSION - the lock held while the channel table is updated
 *
 * MAC_OS_$INIT calls ML_$EXCLUSION_INIT on it at 0x00E2F50A.
 *
 * Address: 0x00E231F8 (MAC_OS_$DATA + 0x868)
 */
ml_$exclusion_t MAC_OS_$EXCLUSION;

/*
 * MAC_OS_$PORT_PKT_TABLES - per-port packet-type tables, block + 0.
 * MAC_OS_$INIT advances its cursor by 0xF4 per port (0x00E2F5E6) and 8 of
 * them fill the block up to MAC_OS_$CHANNEL_TABLE at + 0x7A0.  Zero in the
 * image.
 *
 * Address: 0x00E22990
 */
mac_os_$port_pkt_table_t MAC_OS_$PORT_PKT_TABLES[MAC_OS_MAX_PORTS];

/*
 * MAC_OS_$PORTP_TABLE - one pointer per port at block + 0x87C, filled in by
 * MAC_OS_$INIT (0x00E2F54C-0x00E2F550) with the address of that port's
 * MAC_OS_$PORT_TABLE entry.  Zero in the image.
 *
 * Address: 0x00E2320C
 */
mac_os_$port_info_t *MAC_OS_$PORTP_TABLE[MAC_OS_MAX_PORTS];

/*
 * MAC_OS_$PORT_TABLE - per-port version/config/mtu records at block + 0x89C,
 * stride 8.  Zero in the image; MAC_OS_$INIT sets version to 1 and
 * MAC_OS_$PUT_INFO replaces the whole record.
 *
 * Address: 0x00E2322C
 */
mac_os_$port_info_t MAC_OS_$PORT_TABLE[MAC_OS_MAX_PORTS];

/*
 * MAC_OS_$BROADCAST_NEXTHOP - block + 0x8E0 (0x00E23270), 10 bytes of the
 * twelve that run to the end of the block.  Reproduced from the image:
 *
 *   00e23270  00 00 00 00  ff ff  ff ff ff ff  00 00
 *
 * MAC_$SEND is its only reader (0x00E0BBA6 `pea (0x8e0,A5)`).
 */
const rip_$nexthop_t MAC_OS_$BROADCAST_NEXTHOP = {
    .network = 0x00000000u,
    .host_hi = 0xFFFFu,
    .host_lo = 0xFFFFFFFFu
};
