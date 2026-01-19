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

/* Status code for illegal port type */
#define status_$internet_illegal_port_type  0x2B0004

/* Port type check mask - bits 1 and 2 (port types 1 and 2) */
#define PORT_TYPE_VALID_MASK    0x06

/* Port state check mask for decrement - bits 3 and 5 (states 0x08 and 0x20) */
#define PORT_STATE_DECREMENT_MASK   0x28

/* User port counter and wired page tracking */
#if defined(M68K)
#define ROUTE_$USER_PORT_COUNT_TYPED    (*(int16_t *)0xE87FD4)
#define ROUTE_$PORT_ARRAY_BASE          ((route_$port_t *)0xE2E0A0)
#else
extern int16_t ROUTE_$USER_PORT_COUNT_TYPED;
extern route_$port_t ROUTE_$PORT_ARRAY_BASE[];
#endif

/* RIP update operation codes */
static const uint16_t RIP_OP_DELETE = 0;    /* From 0xe69fb0 */
static const uint16_t RIP_OP_FLAGS = 0;     /* From 0xe69fae */

/* Forward declarations */
void ROUTE_$DECREMENT_PORT(int8_t delete_flag, int16_t port_index, int8_t port_type_flag);
void ROUTE_$CLEANUP_WIRED(void);
void RIP_$UPDATE_D(route_$port_t *port, void *network, const uint16_t *op_delete,
                   route_$short_port_t *short_port, const uint16_t *op_flags,
                   status_$t *status);

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
    uint32_t network_copy;
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
    port = &ROUTE_$PORT_ARRAY_BASE[port_index];

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
     * Save network address and clear low 20 bits of short_port data
     * This masks off the node ID portion, keeping only the network
     */
    network_copy = port->network;
    *(uint32_t *)((uint8_t *)&short_port + 6) &= 0xFFF00000;

    /*
     * Notify RIP of port deletion
     */
    RIP_$UPDATE_D(port, &network_copy, &RIP_OP_DELETE, &short_port,
                  &RIP_OP_FLAGS, status_ret);

    /*
     * For type 2 (routing) ports, close the socket and cleanup
     */
    if (port->port_type == ROUTE_PORT_TYPE_ROUTING) {
        SOCK_$CLOSE(port->socket);
        ROUTE_$USER_PORT_COUNT_TYPED--;

        /*
         * Cleanup wired pages if no more user ports
         */
        ROUTE_$CLEANUP_WIRED();

        /*
         * Clear the driver/callback pointer at offset 0x44
         * Original: *(port+0x44) = NULL via indirect pointer
         */
        /* TODO: Identify this field - appears to be a callback pointer */
    }

    /*
     * Mark port as inactive
     */
    port->active = 0;
}
