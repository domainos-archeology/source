/*
 * NETWORK_$SET_SERVICE - Configure network services
 *
 * Sets or modifies the network service configuration. Supports four
 * operations: OR bits, AND NOT bits, SET value, and SET remote pool.
 *
 * Original address: 0x00E0F45E
 *
 * Assembly analysis shows this function:
 * 1. Acquires spin lock at network data base + 0x2A4
 * 2. Switches on operation code (0-3)
 * 3. For ops 0-2, validates against diskless restrictions
 * 4. For op 3, sets remote pool via MMAP_$REMOTE_POOL
 * 5. If services enabled and routing ports exist, sets routing bit
 * 6. Updates some additional state if DAT_00e2e0ce == 0
 */

#include "network/network.h"

/*
 * Spin lock for network data protection
 * Located at network data base + 0x2A4 = 0xE24BA0
 */
extern void *NETWORK_$LOCK;  /* 0xE24BA0 */

/*
 * External flag checked before additional service updates
 */
extern int16_t DAT_00e2e0ce;  /* 0xE2E0CE */

/*
 * External callback table pointer for additional service notification
 * The function at offset +0x24 is called when service changes
 */
extern void *DAT_00e2e0e8;    /* 0xE2E0E8 */
extern void *DAT_00e2e0d0;    /* 0xE2E0D0 - passed as first arg */

void NETWORK_$SET_SERVICE(int16_t *op_ptr, uint32_t *value_ptr, status_$t *status_p)
{
    ml_$spin_token_t token;
    int16_t op;
    uint32_t value;
    uint16_t new_service;

    op = *op_ptr;
    value = *value_ptr;

    token = ML_$SPIN_LOCK(&NETWORK_$LOCK);

    switch (op) {
    case NETWORK_OP_OR_BITS:
        /*
         * OR value bits into allowed service
         */
        new_service = (uint16_t)NETWORK_$ALLOWED_SERVICE | (uint16_t)value;
        goto update_service;

    case NETWORK_OP_AND_NOT_BITS:
        /*
         * AND NOT - clear bits specified in value
         */
        new_service = (uint16_t)NETWORK_$ALLOWED_SERVICE & ~(uint16_t)value;
        goto check_diskless;

    case NETWORK_OP_SET_VALUE:
        /*
         * Set value directly
         */
        new_service = (uint16_t)value;
        goto check_diskless;

    case NETWORK_OP_SET_REMOTE_POOL:
        /*
         * Set remote pool size via MMAP_$REMOTE_POOL
         */
        ML_$SPIN_UNLOCK(&NETWORK_$LOCK, token);
        NETWORK_$REMOTE_POOL = (int16_t)MMAP_$REMOTE_POOL((uint16_t)(value >> 16));
        *status_p = status_$ok;
        return;

    default:
        /*
         * Invalid operation code
         */
        ML_$SPIN_UNLOCK(&NETWORK_$LOCK, token);
        *status_p = status_$network_unknown_request_type;
        return;
    }

check_diskless:
    /*
     * On diskless nodes (NETWORK_$DISKLESS < 0), paging (bit 0) and
     * file service (bit 2) cannot be disabled. If the new value would
     * disable these, deny the request.
     *
     * Original: if ((NETWORK_$DISKLESS < 0) && ((~new_service & 5) != 0))
     * Bits 0 and 2 = 0x05 (paging + network active)
     */
    if ((NETWORK_$DISKLESS < 0) && ((~new_service & 0x05) != 0)) {
        ML_$SPIN_UNLOCK(&NETWORK_$LOCK, token);
        *status_p = status_$network_request_denied_by_local_node;
        return;
    }

update_service:
    /*
     * Update the allowed service value
     */
    NETWORK_$ALLOWED_SERVICE = (NETWORK_$ALLOWED_SERVICE & 0xFFFF0000) | new_service;

    /*
     * If routing ports exist (N_ROUTING_PORTS > 1) or user socket is open,
     * and some service is enabled, automatically set the routing bit.
     *
     * Original check:
     *   if ((NETWORK_$USER_SOCK_OPEN | -(1 < ROUTE_$N_ROUTING_PORTS)) < 0)
     *      && (NETWORK_$ALLOWED_SERVICE != 0)
     */
    if ((NETWORK_$USER_SOCK_OPEN != 0 || ROUTE_$N_ROUTING_PORTS > 1) &&
        ((uint16_t)NETWORK_$ALLOWED_SERVICE != 0)) {
        NETWORK_$ALLOWED_SERVICE |= NETWORK_SERVICE_ROUTING;  /* Set bit 3 */
    }

    ML_$SPIN_UNLOCK(&NETWORK_$LOCK, token);
    *status_p = status_$ok;

    /*
     * If DAT_00e2e0ce == 0, perform additional service notification.
     * This converts internal service flags to an external format and
     * calls through a callback table.
     *
     * TODO: Implement the additional service notification path.
     * The original code builds a service bitmap with different bit positions
     * and calls through DAT_00e2e0e8 + 0x24 with various parameters.
     */
#if 0
    if (DAT_00e2e0ce == 0) {
        uint16_t external_service = 0;

        if ((NETWORK_$ALLOWED_SERVICE & NETWORK_SERVICE_ACTIVE) != 0) {
            external_service = 0x0B;
            if ((NETWORK_$ALLOWED_SERVICE & NETWORK_SERVICE_PAGING) != 0) {
                external_service = 0x2B;
            }
            if ((NETWORK_$ALLOWED_SERVICE & NETWORK_SERVICE_FILE) != 0) {
                external_service |= 0x10;
            }
            if ((NETWORK_$ALLOWED_SERVICE & NETWORK_SERVICE_ROUTING) != 0) {
                external_service |= 0x04;
            }
            if ((NETWORK_$ALLOWED_SERVICE & 0x10) != 0) {
                external_service |= 0x80;
            }
        }
        /* Call through callback table - implementation TBD */
    }
#endif
}
