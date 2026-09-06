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
 * Main ring data structure.
 * Located at 0xE86400 on original platform.
 */
ring_global_t RING_$DATA;

/*
 * Network UID for ring interface.
 * This is the public UID that identifies the ring network.
 */
uid_t RING_$NETWORK_UID;

/*
 * Template UID for initialization.
 * Located at 0x00E1747C on original platform.
 * Copied to RING_$NETWORK_UID during init.
 */
uid_t RING_$NETWORK_UID_TEMPLATE = UID_CONST(0x00000700, 0);

/*
 * Global network UID storage (referenced by RING_$INIT).
 * Located at 0x00E86960 on original platform.
 */
uid_t ring_$network_uid_storage;

/* The route port array (0x00E2E0A0) is ROUTE_$PORT_ARRAY in route/route_data.c */

/*
 * Device type constant for ring network controller.
 * Used by IO_$GET_DCTE to locate the device.
 */
uint16_t ring_dcte_ctype_net = 0x0001;  /* Network controller type */

/*
 * ============================================================================
 * Error Status Constants
 * ============================================================================
 */

/*
 * Error returned when no socket is available.
 */
status_$t No_available_socket_err = 0x0011000C;

/*
 * Error returned on hardware failure.
 */
status_$t Network_hardware_error = 0x00110001;

/*
 * ============================================================================
 * Global Counters (for external reference)
 * ============================================================================
 *
 * These are aliases to fields in RING_$DATA for convenience.
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
