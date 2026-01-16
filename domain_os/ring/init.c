/*
 * RING_$INIT - Initialize a ring network unit
 *
 * Initializes the ring unit data structures, event counts, and exclusion
 * locks. Creates a network I/O port for the unit. This function is called
 * during system initialization for each token ring controller present.
 *
 * Original address: 0x00E2FAE0
 *
 * Assembly analysis:
 *   - Calculates unit offset: unit_num * 0x244
 *   - Copies RING_$NETWORK_UID to global location
 *   - Stores device_info pointer at unit offset + 0x20
 *   - Initializes 3 event counts at +0x04, +0x10, +0x24
 *   - Initializes 2 exclusion locks at +0x34, +0x4C
 *   - Clears 10 channel entries (loop from +0x08, 8 bytes each, offset +0x5A)
 *   - Calls internal init function (FUN_00e2fa86)
 *   - Creates network I/O port via NET_IO_$CREATE_PORT
 *   - Sets route port pointer at unit offset +0x00
 *   - Sets initialized flag at unit offset +0x60
 */

#include "ring/ring_internal.h"

/*
 * External data referenced by this function
 */

/* Template UID for ring network - copied to global on init */
extern uid_t RING_$NETWORK_UID_TEMPLATE;  /* at 0x00E1747C */

/* Global network UID storage */
extern uid_t ring_$network_uid_storage;   /* at 0x00E86960 */

/* Route port array base */
extern uint8_t ROUTE_$PORT_BASE[];        /* at 0x00E2E0A0 */
#define ROUTE_PORT_SIZE 0x5C              /* Size of each route port entry */

/*
 * Forward declaration for internal initialization helper
 */
static status_$t ring_$init_internal(ring_unit_t *unit_data, void *device_info);

/*
 * RING_$INIT - Initialize a ring unit
 *
 * @param device_info   Device information structure from DCTE
 *                      Contains unit number at offset +6
 *
 * @return status_$ok on success, error code on failure
 */
status_$t RING_$INIT(void *device_info)
{
    int16_t unit_num;
    int unit_offset;
    ring_unit_t *unit_data;
    int16_t port_num;
    status_$t status;
    int i;

    /*
     * Get unit number from device info structure.
     * Unit number is at offset 6 (a short).
     */
    unit_num = *(int16_t *)((uint8_t *)device_info + 6);

    /*
     * Calculate byte offset for this unit's data structure.
     * Each unit has 0x244 bytes of data.
     */
    unit_offset = (int16_t)(unit_num * RING_UNIT_SIZE);

    /*
     * Get pointer to this unit's data structure.
     */
    unit_data = &RING_$DATA.units[unit_num];

    /*
     * Copy the network UID template to the global storage.
     * This is done on each init, so all units share the same UID.
     */
    ring_$network_uid_storage.high = RING_$NETWORK_UID_TEMPLATE.high;
    ring_$network_uid_storage.low = RING_$NETWORK_UID_TEMPLATE.low;

    /*
     * Store the device info pointer in the unit structure.
     */
    unit_data->device_info = device_info;

    /*
     * Initialize the three event counts for this unit:
     *   - rx_wake_ec (+0x04): Receive wake events
     *   - tx_ec (+0x10): Transmit completion events
     *   - ready_ec (+0x24): Ready/initialization events
     */
    EC_$INIT(&unit_data->rx_wake_ec);
    EC_$INIT(&unit_data->tx_ec);
    EC_$INIT(&unit_data->ready_ec);

    /*
     * Initialize the two exclusion locks:
     *   - tx_exclusion (+0x34): Protects transmit operations
     *   - rx_exclusion (+0x4C): Protects receive operations
     */
    ML_$EXCLUSION_INIT(&unit_data->tx_exclusion);
    ML_$EXCLUSION_INIT(&unit_data->rx_exclusion);

    /*
     * Clear all channel entries (10 channels, 8 bytes each).
     * The assembly shows a loop clearing byte at offset +0x5A from
     * each 8-byte channel entry base.
     *
     * Assembly:
     *   moveq #0x9,D0             ; 10 iterations (0-9)
     *   lea (0x8,A3),A0           ; Start at unit + 8
     *   clr.b (0x5a,A0)           ; Clear flags byte
     *   addq.l #0x8,A0            ; Next channel
     *   dbf D0,loop
     *
     * The channel array starts at offset 0x5A within unit_data,
     * with the flags byte at offset 0 of each 8-byte channel entry.
     */
    for (i = 0; i < RING_MAX_CHANNELS; i++) {
        unit_data->channels[i].flags = 0;
    }

    /*
     * Call internal initialization helper.
     * This sets up additional unit-specific data.
     */
    status = ring_$init_internal(unit_data, device_info);

    if (status == status_$ok) {
        /*
         * Create a network I/O port for this unit.
         * Parameters:
         *   - 0: Mode/type
         *   - unit_num: Unit number
         *   - &RING_$DATA + 0x518: Some data address
         *   - 0: Additional parameter
         *   - &status: Status output
         *
         * Returns the port number which is stored in the port array.
         */
        port_num = NET_IO_$CREATE_PORT(0, unit_num,
                                       (void *)((uint8_t *)&RING_$DATA + 0x518),
                                       0, &status);

        /*
         * Store the port number in the per-unit port array.
         */
        RING_$DATA.port_array[unit_num] = port_num;

        /*
         * Set the route port pointer to point into the route port array.
         * Each route port entry is 0x5C bytes, indexed by port_num.
         * Route port base is at 0x00E2E0A0, stored with factor 0x17 words
         * (which equals 0x5C bytes after the multiply by word offset).
         *
         * Assembly: lea (0x0,A1,D1*0x1),A1 where D1 = port_num * 0x5C
         */
        unit_data->route_port = (void *)(ROUTE_$PORT_BASE + port_num * ROUTE_PORT_SIZE);

        if (status == status_$ok) {
            /*
             * Mark unit as initialized.
             * The initialized flag is -1 (0xFF) when set.
             */
            unit_data->initialized = (int8_t)-1;  /* 0xFF = true */
        }
    }

    return status;
}

/*
 * ring_$init_internal - Internal initialization helper
 *
 * This corresponds to FUN_00e2fa86 in the original.
 * Sets up additional unit-specific data based on the IIC data.
 *
 * @param unit_data     Unit data structure
 * @param device_info   Device information
 *
 * @return status code
 *
 * Original address: 0x00E2FA86
 */
static status_$t ring_$init_internal(ring_unit_t *unit_data, void *device_info)
{
    /*
     * TODO: This function needs further analysis.
     * It appears to set up IIC (Inter-IC Communication) related data
     * structures for the ring controller.
     *
     * For now, return success to allow basic initialization to proceed.
     */
    (void)unit_data;
    (void)device_info;

    return status_$ok;
}
