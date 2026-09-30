/*
 * net_io/net_io_data.c - NET_IO module data
 *
 * Two segments of the SAU2 map:
 *
 *   D E244F0  NET_IO          size = AC
 *     E244F0  NET_IO_$ALL_F_ADDR
 *     E244F4  NET_IO_$NIL_DRIVER     0x50 bytes
 *     E24544  NET_IO_$USER_DRIVER    0x50 bytes
 *     E24594  RING_$OVERFLOW_OVERFLOW  (also RING_$FILE_OVERFLOW, RING_$DELIVERY_FAILED)
 *
 *   D E81668  NET_IO_UNWIRED  size = 14
 *
 * The two driver blocks are net_io_$driver_t records; see net_io/net_io.h for
 * how each slot was identified.  The values below are exactly the image
 * bytes, with the four non-zero procedure variables written as the symbols
 * the map gives their targets.
 */

#include "net_io/net_io_internal.h"
#include "ring/ring.h"   /* RING_$OVERFLOW_OVERFLOW .. RING_$DELIVERY_FAILED */

/* NET_IO_$ALL_F_ADDR (0xE244F0): `gsk read 0xE244F0 4` = 00 0f ff ff */
uint32_t NET_IO_$ALL_F_ADDR = 0x000FFFFF;

/*
 * NET_IO_$NIL_DRIVER - the driver block ROUTE_$SERVICE hands
 * NET_IO_$CREATE_PORT for port type 1 (0x00E6A158).
 *
 *   00e244f4  00 02 10 00 14 00 00 00  00 00 00 00 00 00 00 00
 *   00e24504  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
 *   00e24514  00 e7 4e c8 00 00 00 00  00 00 00 00 00 00 00 00
 *   00e24524  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
 *   00e24534  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
 *
 * A local port neither transmits nor keeps statistics, so every slot but the
 * per-address-space teardown is nil.  The network_uid tail is zero in the
 * image; NET_IO_$BOOT_DEVICE fills it with NIL_$NETWORK_UID at boot
 * (0x00E31C28 writes 0xE2453C and 0xE24540).
 *
 * Original address: 0xE244F4
 */
net_io_$driver_t NET_IO_$NIL_DRIVER[1] = { {
    ._unknown0      = 0x0002,
    .max_data_len   = 0x1000,
    .mtu            = 0x1400,
    ._unknown6      = 0x00,
    .flags          = 0x00,
    .sendp          = NULL,
    .get_stats      = NULL,
    .get_stats2     = NULL,
    .start          = NULL,
    .stop           = NULL,
    .detach         = NULL,
    .proc2_cleanup  = (net_io_$driver_fn_t)NET_IO_$CLEANUP_NIL,   /* 0x00E74EC8 */
    .ioctl          = NULL,
    .svc_open       = NULL,
    .svc_close      = NULL,
    .svc_ioctl      = NULL,
    .svc_write      = NULL,
    .svc_read       = NULL,
    .open_os        = NULL,
    .close_os       = NULL,
    .send_os        = NULL,
    .network_uid    = { 0, 0 },
} };

/*
 * NET_IO_$USER_DRIVER - the driver block ROUTE_$SERVICE hands
 * NET_IO_$CREATE_PORT for user routing ports (0x00E6A162).
 *
 *   00e24544  00 02 04 00 00 00 00 00  00 e8 7c 34 00 e6 a6 5e
 *   00e24554  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
 *   00e24564  00 e7 4f 1e 00 00 00 00  00 00 00 00 00 00 00 00
 *   00e24574  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
 *   00e24584  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
 *
 * A user routing port transmits through the routing code itself, so its
 * transmit and statistics slots are ROUTE_ entry points rather than device
 * ones.  NET_IO_$BOOT_DEVICE fills the network_uid tail with
 * USER_$NETWORK_UID (0x00E31C3A writes 0xE2458C and 0xE24590).
 *
 * Original address: 0xE24544
 */
net_io_$driver_t NET_IO_$USER_DRIVER[1] = { {
    ._unknown0      = 0x0002,
    .max_data_len   = 0x0400,
    .mtu            = 0x0000,
    ._unknown6      = 0x00,
    .flags          = 0x00,
    .sendp          = (net_io_$driver_fn_t)ROUTE_$SEND_USER_PORT,  /* 0x00E87C34 */
    .get_stats      = (net_io_$driver_fn_t)ROUTE_$READ_USER_STATS, /* 0x00E6A65E */
    .get_stats2     = NULL,
    .start          = NULL,
    .stop           = NULL,
    .detach         = NULL,
    .proc2_cleanup  = (net_io_$driver_fn_t)NET_IO_$CLEANUP_USER,   /* 0x00E74F1E */
    .ioctl          = NULL,
    .svc_open       = NULL,
    .svc_close      = NULL,
    .svc_ioctl      = NULL,
    .svc_write      = NULL,
    .svc_read       = NULL,
    .open_os        = NULL,
    .close_os       = NULL,
    .send_os        = NULL,
    .network_uid    = { 0, 0 },
} };

/*
 * The rest of the NET_IO segment: three receive-overflow counters the map
 * files under RING_ (declared in ring/ring.h).  `gsk read 0xE24594 8`:
 * 00 00 00 00 00 00 00 00, so all three start at zero.  Plain objects, as
 * nothing bases A5 on this segment (their map-order placement is
 * source-91vs).  Until the per-process-data step for ring (source-vulx)
 * the m68k build reached them through absolute-address macros.
 *
 * Original addresses: 0xE24594, 0xE24596, 0xE24598
 */
uint16_t RING_$OVERFLOW_OVERFLOW;
uint16_t RING_$FILE_OVERFLOW;
uint16_t RING_$DELIVERY_FAILED;

/*
 * NET_IO_UNWIRED - the module's unwired block.
 *
 *   00e81668  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
 *   00e81678  03 e7 00 00
 *
 * The only non-zero image word is boot_unit = 0x3E7 (999), the sentinel
 * NET_IO_$CREATE_PORT reads at 0x00E5A50C when no network boot device was
 * recorded.  0xE8167C, the next byte, is CAL_$WRITE_CALENDAR, so the block
 * really is the 0x14 bytes the map gives it.
 *
 * Original address: 0xE81668
 */
net_io_unwired_t NET_IO_UNWIRED = {
    .port_asid      = { 0, 0, 0, 0, 0, 0, 0, 0 },
    .boot_unit      = NET_IO_$NO_BOOT_UNIT,
    .boot_port_type = 0,
};

/*
 * net_io_$route_op_close - the ROUTE_$SERVICE operation word 0x0008 at
 * 0x00E74F1C, in the code region between NET_IO_$CLEANUP_NIL and
 * NET_IO_$CLEANUP_USER; both pass its address (see net_io_internal.h).
 */
const uint16_t net_io_$route_op_close = 0x0008;
