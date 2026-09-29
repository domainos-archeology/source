/*
 * Ring module global data
 *
 * Module data blocks RING_$CTL and RING_$WIRED_DATA: Claude Opus 5.5
 * (source-vulx).
 *
 * The ring driver's two data segments (docs/design-per-process-data.md), as
 * MODULE_DATA blocks that build/sau2/layout.ld links in the SAU2 map's
 * order; the address is the ordering key, not the link address.  Layouts and
 * asserts in ring/ring.h.
 *
 *   0x00E261AC  RING_$WIRED_DATA  map "D E261AC RING_WIRED size = AC"
 *   0x00E86400  RING_$CTL         map "D30 E86400 RING_DATA loaded at 187C00,
 *                                 size = 5D0" (RING_DATA_START .. _END)
 *
 * The map's third RING data segment, "D E35028 RING size = 4" in
 * OS_INIT_DATA, has no reference anywhere in the image (gsk xrefs), so it
 * has no object here.
 */

#include "ring/ring_internal.h"

/*
 * RING_$WIRED_DATA, 0x00E261AC..0x00E26257 (`gsk read 0xE261AC 172`): zero
 * except three words, all 1:
 *
 *   0x00E261C2  +0x16  00 01   swdiag._r00
 *   0x00E261E0  +0x34  00 01   stats[0]._reserved0
 *   0x00E2621C  +0x70  00 01   stats[1]._reserved0
 */
MODULE_DATA_DEFINE_INIT(ring_$wired_data_t, RING_$WIRED_DATA, 0x00E261AC, {
    .swdiag = { ._r00 = 1 },
    .stats  = { { ._reserved0 = 1 }, { ._reserved0 = 1 } },
});

/*
 * RING_$CTL, 0x00E86400..0x00E869CF (`gsk read 0xE86400 1488`): the unit
 * records, scrub/from_err cells and wire list are zero; the non-zero bytes
 * are
 *
 *   +0x518  00 02 04 00 00 00 00 03       the driver record's head:
 *                                         _unknown0 2, max_data_len 0x400,
 *                                         mtu 0, flags 3
 *   +0x520  00 e7 59 16  00 e7 69 50      RING_$SENDP, RING_$GET_STATS
 *   +0x528  00 e7 69 50  00 e7 68 30      RING_$GET_STATS, RING_$START
 *   +0x530  00 e7 69 c4  00 00 00 00      RING_$STOP, nil
 *   +0x538  00 e7 6a 42  00 e7 6b 2c      RING_$PROC2_CLEANUP, RING_$IOCTL
 *   +0x540  00 e7 6d f2  00 e7 6e 22      RING_$SVC_OPEN, RING_$SVC_CLOSE
 *   +0x548  00 e7 76 b8  00 e7 6f 9e      RING_$SVC_IOCTL, RING_$SVC_WRITE
 *   +0x550  00 e7 74 02  00 e7 7b a0      RING_$SVC_READ, RING_$OPEN_OS
 *   +0x558  00 e7 7c 24  00 e7 7c 60      RING_$CLOSE_OS, RING_$SEND_OS
 *   +0x560  8 zero bytes                  network_uid (RING_$INIT fills it)
 *   +0x570  00 00 00 00 00 89             _r570          { 0, 0x0089 }
 *   +0x578  00 00 00 00 04 22             xmit_timeout1  { 0, 0x0422 }
 *   +0x580  00 00 00 00 02 ab             xmit_timeout2  { 0, 0x02AB }
 *   +0x588  00 00 00 00 02 ab             _r588          { 0, 0x02AB }
 *   +0x590  00 00 00 00 01 77             poll_timeout   { 0, 0x0177 }
 *   +0x598  00 00 00 05 9e 22             wait_timeout   { 5, 0x9E22 }
 *   +0x5C8  00 e7 66 42  00 e7 66 5e      rcv_proc: RING_$RCV0, RING_$RCV1
 *
 * force_start_timeout (+0x568) is zero.  The procedure variables name the
 * routines the map gives their targets, as net_io/net_io_data.c does for
 * the NIL and USER driver records.
 */
MODULE_DATA_DEFINE_INIT(ring_global_t, RING_$CTL, 0x00E86400, {
    .driver = {
        ._unknown0      = 0x0002,
        .max_data_len   = 0x0400,
        .mtu            = 0x0000,
        ._unknown6      = 0x00,
        .flags          = 0x03,
        .sendp          = (net_io_$driver_fn_t)RING_$SENDP,         /* 0x00E75916 */
        .get_stats      = (net_io_$driver_fn_t)RING_$GET_STATS,     /* 0x00E76950 */
        .get_stats2     = (net_io_$driver_fn_t)RING_$GET_STATS,     /* 0x00E76950 */
        .start          = (net_io_$driver_fn_t)RING_$START,         /* 0x00E76830 */
        .stop           = (net_io_$driver_fn_t)RING_$STOP,          /* 0x00E769C4 */
        .detach         = NULL,
        .proc2_cleanup  = (net_io_$driver_fn_t)RING_$PROC2_CLEANUP, /* 0x00E76A42 */
        .ioctl          = (net_io_$driver_fn_t)RING_$IOCTL,         /* 0x00E76B2C */
        .svc_open       = (net_io_$driver_fn_t)RING_$SVC_OPEN,      /* 0x00E76DF2 */
        .svc_close      = (net_io_$driver_fn_t)RING_$SVC_CLOSE,     /* 0x00E76E22 */
        .svc_ioctl      = (net_io_$driver_fn_t)RING_$SVC_IOCTL,     /* 0x00E776B8 */
        .svc_write      = (net_io_$driver_fn_t)RING_$SVC_WRITE,     /* 0x00E76F9E */
        .svc_read       = (net_io_$driver_fn_t)RING_$SVC_READ,      /* 0x00E77402 */
        .open_os        = (net_io_$driver_fn_t)RING_$OPEN_OS,       /* 0x00E77BA0 */
        .close_os       = (net_io_$driver_fn_t)RING_$CLOSE_OS,      /* 0x00E77C24 */
        .send_os        = (net_io_$driver_fn_t)RING_$SEND_OS,       /* 0x00E77C60 */
        .network_uid    = { 0, 0 },
    },
    ._r570          = { .high = 0, .low = 0x0089 },
    .xmit_timeout1  = { .high = 0, .low = 0x0422 },
    .xmit_timeout2  = { .high = 0, .low = 0x02AB },
    ._r588          = { .high = 0, .low = 0x02AB },
    .poll_timeout   = { .high = 0, .low = 0x0177 },
    .wait_timeout   = { .high = 5, .low = 0x9E22 },
    .rcv_proc       = { RING_$RCV0, RING_$RCV1 },   /* 0x00E76642, 0x00E7665E */
});

/*
 * RING_$NETWORK_UID - the ring network's UID.
 *
 * SAU2 map: `E1747C RING_$NETWORK_UID`, one of the canned UIDs in the
 * OS_WIRED uid table (OS_PG_FILE_$UID 0xE173CC .. ).  Image bytes at
 * 0x00E1747C: 00 00 07 00 00 00 00 00, i.e. { high = 0x00000700, low = 0 }.
 * It is a constant in the code image; RING_$INIT copies it into
 * RING_$CTL.network_uid (`movea.l #0xe1747c,A1 / move.l (A1)+,(0x560,A0) /
 * move.l (A1)+,(0x564,A0)` at 0x00E2FAFA-0x00E2FB10, A0 = 0xE86400).
 *
 * Earlier versions of this file split it into a "template" plus a separate
 * `ring_$network_uid_storage` at 0xE86960 - that address is RING_$CTL +
 * 0x560, not a second cell.
 *
 * Original address: 0x00E1747C
 */
uid_t RING_$NETWORK_UID = UID_CONST(0x00000700, 0);

/* The route port array (0x00E2E0A0) is ROUTE_$PORT_ARRAY in route/route_data.c */

/*
 * Device type constant for ring network controller.
 * Used by IO_$GET_DCTE to locate the device.
 */
uint16_t ring_dcte_ctype_net = 0x0002;  /* 0x00E7628A: the literal cell the
                                         * receive daemon passes by reference
                                         * to IO_$GET_DCTE ("pea (0x21c,PC)"
                                         * at 0x00E7606C) */

/*
 * ============================================================================
 * Error Status Constants
 * ============================================================================
 */

/*
 * Error returned on hardware failure.
 */
status_$t Network_hardware_error = 0x00110001;

