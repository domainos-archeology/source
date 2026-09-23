/*
 * Ring module global data
 *
 * This file defines the global data structures used by the ring module.
 * These correspond to memory-mapped data at specific addresses on the
 * original m68k platform.
 */

#include "ring/ring_internal.h"

/*
 * ============================================================================
 * Global Data Structures
 * ============================================================================
 */

/*
 * Main ring control structure.
 *
 * The SAU2 map (~/src/domainos-archeology/sau2-maps/domain_os.10.2.map) names
 * this segment and its one exported symbol:
 *
 *   D30  E86400  RING_DATA          loaded at 187C00, size = 5D0
 *   D    E86400  RING               size = 5D0
 *        E86400  RING_$CTL
 *
 * so 0xE86400 is RING_$CTL, not RING_$DATA (that name belongs to the
 * per-unit statistics array at 0xE261E0, below).
 */
ring_global_t RING_$CTL;

/*
 * Per-unit statistics array, 0x3C bytes per unit.
 *
 * SAU2 map:
 *
 *        E261E0  RING_$DATA                       MARKED
 *
 * i.e. the last object in the `D E261AC RING_WIRED size = AC` segment.
 */
ring_$stats_t RING_$DATA[RING_MAX_UNITS];

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

/*
 * ============================================================================
 * Global Counters (for external reference)
 * ============================================================================
 *
 * These are aliases to fields in RING_$CTL for convenience.
 * The actual counters are in the ring_global_t structure.
 */

/*
 * NETWORK_$ACTIVITY_FLAG (0x00E24C42) is defined in network/network_data.c.
 */

/*
 * ============================================================================
 * Transmit Statistics (shared across units)
 * ============================================================================
 *
 * These global counters track transmit events across all units.
 * Located in the global data area (0xE261BC-0xE261BE range).
 */

/*
 * Global biphase error count.
 */
uint16_t RING_$GLOBAL_BIPHASE_CNT;

/*
 * Global ESB error count.
 */
uint16_t RING_$GLOBAL_ESB_CNT;

/*
 * ============================================================================
 * Software-diagnostic counters (`D E261AC RING_WIRED size = AC`)
 * ============================================================================
 *
 * The SAU2 map names every cell in this segment:
 *
 *   E261AC  RING_$SWDIAG_NODEID       MARKED
 *   E261B0  RING_$SWDIAG_GOODRCV_CNT  MARKED
 *   E261B4  RING_$SWDIAG_RCVCNT       MARKED
 *   E261B8  RING_$RCV_BIPHASE         MARKED
 *   E261BA  RING_$RCV_ESB             MARKED
 *   E261BC  RING_$XMIT_BIPHASE
 *   E261BE  RING_$XMIT_ESB
 *   E261C0  RING_$PAGING_OVERFLOW     MARKED
 *   E261C2  RING_$SWDIAG_DATA         MARKED
 *   E261E0  RING_$DATA                MARKED
 *
 * so RING_$SWDIAG_DATA runs 0xE261C2..0xE261E0 - the 0x1E bytes
 * ring_$swdiag_t describes - and each of the four biphase/ESB counters is a
 * single word.
 *
 * Image contents (gsk read 0x00E261AC 60): the whole segment is zero except
 * RING_$SWDIAG_DATA._r00, which reads 0x0001.
 */

/*
 * Per-line biphase-violation and end-of-single-bit error counts.  Bumped
 * alongside the per-unit statistics; see ring/ring.h.
 *
 * Original addresses: 0xE261B8, 0xE261BA, 0xE261BC, 0xE261BE
 */
uint16_t RING_$RCV_BIPHASE;
uint16_t RING_$RCV_ESB;
uint16_t RING_$XMIT_BIPHASE;
uint16_t RING_$XMIT_ESB;

/*
 * RING_$SWDIAG_DATA - the software-diagnostic receive-error mirror block that
 * NETWORK_$PROCESS_PAGING_REQUEST (0x00E11278) and ASKNODE_$INTERNET_INFO
 * (0x00E64B68) copy whole into their replies.
 *
 * Original address: 0xE261C2 (0x1E bytes)
 */
ring_$swdiag_t RING_$SWDIAG_DATA = { ._r00 = 1 };
