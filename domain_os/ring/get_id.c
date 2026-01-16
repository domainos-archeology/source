/*
 * RING_$GET_ID - Get the network ID for a ring unit
 *
 * Retrieves the 24-bit network ID from the device's hardware address.
 * The ID is constructed from bytes at offsets 0x12, 0x14, and 0x16
 * in the device initialization data (disk_dinit field of DCTE).
 *
 * The network ID identifies the node on the token ring network.
 * It is derived from the hardware's burned-in address.
 *
 * Original address: 0x00E2FA28
 *
 * Assembly analysis:
 *   - Calls IO_$GET_DCTE to get device control table entry
 *   - If status != status_$io_controller_not_in_system:
 *     - Gets disk_dinit pointer from DCTE offset 0x34
 *     - Extracts 3 bytes from offsets 0x12, 0x14, 0x16
 *     - Combines into 24-bit network ID (0x12 << 16 | 0x14 << 8 | 0x16)
 *     - Stores result at disk_dinit + 8
 *   - Returns the network ID (or 1 if controller not in system)
 */

#include "ring/ring_internal.h"

/*
 * Device type constant for ring network controller.
 * Located at 0x00E2FA84 (2 bytes after the SHORT_00e2fa84 label).
 */
static uint16_t ring_dcte_ctype = 0; /* Initialized elsewhere */

/*
 * DCTE structure offsets (partial - for ring use).
 * The full structure is defined in disk.h but we only need
 * the disk_dinit field here.
 */
typedef struct local_dcte_t {
    uint8_t     _reserved[0x34];
    void        *disk_dinit;        /* 0x34: Pointer to device init data */
} local_dcte_t;

/*
 * IO_$GET_DCTE - Get device control table entry.
 *
 * On m68k, this function returns the DCTE pointer in register A0.
 * The status is returned via the status pointer parameter.
 *
 * This is declared locally since the signature varies by usage context.
 */
extern local_dcte_t *IO_$GET_DCTE(void *type, void *addr, status_$t *status);

/*
 * RING_$GET_ID - Get network ID from device hardware
 *
 * @param param      Pointer to unit info (used to find DCTE)
 * @return           24-bit network ID, or 1 if not found
 */
uint32_t RING_$GET_ID(void *param)
{
    local_dcte_t *dcte;
    status_$t status;
    uint32_t net_id = 1;  /* Default return if controller not in system */
    uint8_t *dinit;

    /*
     * Get the Device Control Table Entry for this ring unit.
     * The ring_dcte_ctype identifies the network controller type.
     */
    dcte = IO_$GET_DCTE(&ring_dcte_ctype, param, &status);

    /*
     * If the controller is in the system, extract the network ID
     * from the device initialization data.
     */
    if (status != status_$io_controller_not_in_system) {
        dinit = (uint8_t *)dcte->disk_dinit;

        /*
         * Build 24-bit network ID from 3 bytes in device init data:
         *   Byte at 0x12 -> bits 23-16 (most significant)
         *   Byte at 0x14 -> bits 15-8
         *   Byte at 0x16 -> bits 7-0 (least significant)
         *
         * This corresponds to the assembly:
         *   move.b (0x12,A1),D2b    ; Get high byte
         *   swap D2                  ; Shift to bits 23-16
         *   clr.w D2w               ; Clear low word
         *   move.b (0x14,A1),D0b    ; Get middle byte
         *   lsl.w #0x8,D0w          ; Shift to bits 15-8
         *   add.l D1,D2             ; Combine
         *   move.b (0x16,A1),D1b    ; Get low byte
         *   add.l D1,D2             ; Combine
         */
        net_id = ((uint32_t)dinit[0x12] << 16) |
                 ((uint32_t)dinit[0x14] << 8) |
                 ((uint32_t)dinit[0x16]);

        /*
         * Store the computed network ID back into the device init data
         * at offset 8. This caches the ID for future use.
         */
        *(uint32_t *)(dinit + 8) = net_id;
    }

    return net_id;
}
