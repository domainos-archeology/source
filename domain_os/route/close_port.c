/*
 * ROUTE_$CLOSE_PORT - Close and remove a routing port
 *
 * This function is called from ROUTE_$SERVICE when bit 3 (0x08) is set
 * in the operation flags. It closes a routing port, cleaning up all
 * associated resources.
 *
 * The function:
 *   1. Finds the port by network/socket identifiers
 *   2. Validates the port type (must be 1 or 2)
 *   3. If port is in certain states, decrements port counters
 *   4. Calls RIP_$UPDATE_D to notify RIP of the removal
 *   5. For type 2 ports, closes the socket
 *   6. Clears the port's active flag
 *
 * Parameters are passed via the caller's stack frame (A6/A2):
 *   - port_info at (0xc,A2): Contains network (+6) and socket (+8)
 *   - status_ret at (0x10,A2): Output status
 *
 * Original address: 0x00E69EC2
 *
 * Called from:
 *   - ROUTE_$SERVICE at 0x00E6A05E (when operation bit 3 is set)
 */

#include "route/route_internal.h"
#include "rip/rip.h"
#include "sock/sock.h"

/* Port type check mask - bits 1 and 2 (port types 1 and 2) */
#define PORT_TYPE_VALID_MASK    0x06

/* Port state check mask for decrement - bits 3 and 5 (states 0x08 and 0x20) */
#define PORT_STATE_DECREMENT_MASK   0x28

/*
 * Note: ROUTE_$N_USER_PORTS and ROUTE_$PORT_ARRAY are defined in route_internal.h
 */

/* RIP update constants (PC-relative data in the original) */
static const uint16_t RIP_HOP_COUNT_ZERO = 0;   /* From 0xe69fb0 (pea (0x48,PC)) */
static const int8_t RIP_OP_FLAGS = 0;           /* From 0xe69fae (pea (0x4e,PC)) */

/*
 * Note: Function declarations come from headers:
 *   - ROUTE_$DECREMENT_PORT from route/route.h, ROUTE_$CLEANUP_WIRED from route/route_internal.h
 *   - RIP_$UPDATE_D from rip/rip.h
 */

/*
 * ROUTE_$CLOSE_PORT - Close a routing port
 *
 * This function accesses parameters from the parent's stack frame,
 * which is why it has unusual parameter access patterns in the
 * decompiled code.
 *
 * The actual parameters are:
 *   - port_info: pointer to port info structure with network at +6, socket at +8
 *   - status_ret: output status pointer
 *
 * These are accessed via A2 (parent frame pointer).
 */
void ROUTE_$CLOSE_PORT(void *port_info, status_$t *status_ret)
{
    int16_t port_index;
    route_$port_t *port;
    route_$short_port_t short_port;
    rip_$xns_addr_t source;         /* -0x10: source XNS address for RIP_$UPDATE_D */
    uint16_t port_network;
    int16_t port_socket;
    uint16_t port_state;

    /*
     * Extract network and socket from port_info structure
     * Layout: +6 = network (uint16_t), +8 = socket (int16_t)
     */
    port_network = *(uint16_t *)((uint8_t *)port_info + 6);
    port_socket = *(int16_t *)((uint8_t *)port_info + 8);

    /*
     * Find the port by network/socket
     * Socket is sign-extended to 32 bits for the search
     */
    port_index = ROUTE_$FIND_PORT(port_network, (int32_t)port_socket);

    if (port_index == -1) {
        *status_ret = status_$internet_unknown_network_port;
        return;
    }

    /*
     * Validate port type - must be type 1 or 2
     * Check if bit (network & 0x1f) is set in mask 0x06
     * This allows port types 1 and 2 only
     */
    if (((1 << (port_network & 0x1f)) & PORT_TYPE_VALID_MASK) == 0) {
        *status_ret = status_$internet_illegal_port_type;
        return;
    }

    /*
     * Get pointer to the port structure
     */
    port = &ROUTE_$PORT_ARRAY[port_index];

    /*
     * Check if port is in a state requiring decrement notification
     * The original code checks port_info at offset -0x62 from parent frame,
     * which contains the port state. States 0x08 and 0x20 require notification.
     */
    port_state = port->active;
    if (((1 << (port_state & 0x1f)) & PORT_STATE_DECREMENT_MASK) != 0) {
        /* Notify that port is being removed - delete_flag=0xFF */
        ROUTE_$DECREMENT_PORT(0xFF, port_index, 0);
    }

    /*
     * Build short port info for RIP notification
     */
    ROUTE_$SHORT_PORT(port, &short_port);

    /*
     * Build the source XNS address in the local frame: the network comes
     * from the port and the low 20 bits of the last longword of the host
     * part are cleared (the remaining host bytes are uninitialized stack
     * data in the original).
     *
     *   00e69f4e    move.l (A3),(-0x10,A6)
     *   00e69f52    andi.l #-0x100000,(-0xa,A6)
     */
    source.network = port->network;
    {
        uint32_t host_tail = ((uint32_t)source.host[2] << 24) |
                             ((uint32_t)source.host[3] << 16) |
                             ((uint32_t)source.host[4] << 8) |
                             (uint32_t)source.host[5];
        host_tail &= 0xFFF00000;
        source.host[2] = (uint8_t)(host_tail >> 24);
        source.host[3] = (uint8_t)(host_tail >> 16);
        source.host[4] = (uint8_t)(host_tail >> 8);
        source.host[5] = (uint8_t)host_tail;
    }

    /*
     * Notify RIP of port deletion
     *
     *   00e69f5a    move.l (0x10,A2),-(SP)    ; status_ret
     *   00e69f5e    pea (0x4e,PC)             ; &RIP_OP_FLAGS
     *   00e69f62    pea (-0x48,A2)            ; &short_port
     *   00e69f66    pea (0x48,PC)             ; &RIP_HOP_COUNT_ZERO
     *   00e69f6a    pea (-0x10,A6)            ; &source
     *   00e69f6e    pea (A3)                  ; &port->network (port entry)
     */
    RIP_$UPDATE_D(&port->network, &source, &RIP_HOP_COUNT_ZERO,
                  (uint8_t *)&short_port, &RIP_OP_FLAGS, status_ret);

    /*
     * For type 2 (routing) ports, close the socket and cleanup
     */
    if (port->port_type == ROUTE_PORT_TYPE_ROUTING) {
        SOCK_$CLOSE(port->socket);
        ROUTE_$N_USER_PORTS--;

        /*
         * Cleanup wired pages if no more user ports
         */
        ROUTE_$CLEANUP_WIRED();

        /*
         * Clear the driver/callback pointer at offset 0x44
         * Original: *(port+0x44) = NULL via indirect pointer
         */
        /* TODO(source-qvt): Identify this field - appears to be a callback pointer */
    }

    /*
     * Mark port as inactive
     */
    port->active = 0;
}
