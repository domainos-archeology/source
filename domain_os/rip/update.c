/*
 * RIP_$UPDATE and RIP_$UPDATE_D - Routing table update wrappers
 *
 * These functions provide external interfaces for updating the routing
 * table. They wrap RIP_$UPDATE_INT with parameter marshaling and
 * port lookup functionality.
 *
 * RIP_$UPDATE_D: Debug/detailed variant with full port identification
 * RIP_$UPDATE:   Simplified variant using port index directly
 *
 * Original addresses:
 *   RIP_$UPDATE_D: 0x00E69084
 *   RIP_$UPDATE:   0x00E690EE
 */

#include "rip/rip_internal.h"

/*
 * ROUTE_$PORT_ARRAY - Array of network port structures
 *
 * This is the base address for the array of route_$port_t structures.
 * Each entry is 0x5C (92) bytes. The first 4 bytes of each entry
 * contain the network number associated with that port.
 *
 * Note: This overlaps with ROUTE_$PORT at 0xE2E0A0, but is used as
 * an array base rather than a single value.
 *
 * TODO: Consolidate with route.h definitions once structure is better understood
 */
#if defined(ARCH_M68K)
    #define ROUTE_$PORT_ARRAY       ((uint8_t *)0xE2E0A0)
#else
    extern uint8_t *ROUTE_$PORT_ARRAY;
#endif

/* Size of each route_$port_t entry */
#define ROUTE_$PORT_ENTRY_SIZE  0x5C

/*
 * RIP_$UPDATE_D - Update routing table with full port identification
 *
 * This function updates the routing table using network/socket pair
 * to identify the port. It first looks up the port index using
 * ROUTE_$FIND_PORT, then delegates to RIP_$UPDATE_INT.
 *
 * The "D" suffix likely stands for "debug" or "detailed" as this variant
 * provides more explicit port identification than RIP_$UPDATE.
 *
 * @param network_ptr    Pointer to destination network (4 bytes)
 * @param source         Pointer to source XNS address (10 bytes)
 * @param hop_count_ptr  Pointer to hop count / metric (2 bytes)
 * @param port_info      Pointer to structure containing:
 *                         +0x06: port network (2 bytes)
 *                         +0x08: port socket (2 bytes)
 * @param flags_ptr      Pointer to route type flags (1 byte):
 *                         If < 0: non-standard route
 *                         If >= 0: standard route
 * @param status_ret     Output: status code
 *
 * Status codes:
 *   status_$ok: Success
 *   status_$internet_unknown_network_port (0x2B0003): Port not found
 *
 * Original address: 0x00E69084
 */
void RIP_$UPDATE_D(uint32_t *network_ptr, void *source_ptr,
                   uint16_t *hop_count_ptr, uint8_t *port_info,
                   int8_t *flags_ptr, status_$t *status_ret)
{
    rip_$xns_addr_t *source = (rip_$xns_addr_t *)source_ptr;
    int16_t port_index;
    uint16_t port_network;
    int32_t port_socket;
    status_$t status;

    /*
     * Extract port network and socket from the port_info structure.
     * The port_info structure has:
     *   +0x06: network (2 bytes, unsigned)
     *   +0x08: socket (2 bytes, sign-extended to 4 bytes)
     *
     * Assembly reference:
     *   00e69092    move.w (0x8,A2),D0w      ; D0 = port_socket
     *   00e69096    ext.l D0                  ; sign-extend to 32 bits
     *   00e6909a    move.w (0x6,A2),-(SP)    ; push port_network
     */
    port_network = *(uint16_t *)(port_info + 0x06);
    port_socket = (int16_t)*(uint16_t *)(port_info + 0x08);

    /* Look up port index by network/socket pair */
    port_index = ROUTE_$FIND_PORT(port_network, port_socket);

    if (port_index == -1) {
        /* Port not found */
        status = status_$internet_unknown_network_port;
    } else {
        /* Delegate to RIP_$UPDATE_INT */
        RIP_$UPDATE_INT(*network_ptr, source, *hop_count_ptr,
                        (uint8_t)port_index, *flags_ptr, &status);
    }

    *status_ret = status;
}

/*
 * RIP_$UPDATE - Update routing table with port index
 *
 * This function updates the routing table using a port index directly.
 * It constructs the source XNS address from the port's network number
 * and the provided host ID, then delegates to RIP_$UPDATE_INT.
 *
 * The source address construction:
 * - Network (4 bytes): Copied from ROUTE_$PORT_ARRAY[port_index]
 * - Host (6 bytes): First 2 bytes cleared (0), last 4 bytes from host_id
 *                   with only the low 20 bits preserved
 *
 * This simplified interface is used for standard routes (flags=0).
 *
 * @param network_ptr    Pointer to destination network (4 bytes)
 * @param host_id_ptr    Pointer to source host ID (low 20 bits used)
 * @param hop_count_ptr  Pointer to hop count / metric (2 bytes)
 * @param port_index_ptr Pointer to port index (0-7)
 * @param status_ret     Output: status code
 *
 * Original address: 0x00E690EE
 */
void RIP_$UPDATE(uint32_t *network_ptr, uint32_t *host_id_ptr,
                 uint16_t *hop_count_ptr, int16_t *port_index_ptr,
                 status_$t *status_ret)
{
    rip_$xns_addr_t source;
    status_$t status;
    int16_t port_index;
    uint32_t host_id;
    uint8_t *port_entry;

    port_index = *port_index_ptr;
    host_id = *host_id_ptr;

    /*
     * Get the network number from the port entry.
     * ROUTE_$PORT_ARRAY[port_index * 0x5C] contains the network
     * number as the first 4 bytes.
     *
     * Assembly reference:
     *   00e690fa    moveq #0x5c,D0
     *   00e690fc    movea.l #0xe2e0a0,A0
     *   00e69102    mulu.w (A2),D0           ; D0 = port_index * 0x5C
     *   00e69104    move.l (0x0,A0,D0w*0x1),(-0x10,A6)
     */
    port_entry = ROUTE_$PORT_ARRAY + (port_index * ROUTE_$PORT_ENTRY_SIZE);
    source.network = *(uint32_t *)port_entry;

    /*
     * Construct the host portion of the source address.
     *
     * The host field (6 bytes) is constructed as:
     * - Bytes 0-1: Uninitialized (implicitly 0 from stack in original)
     * - Bytes 2-5: Low 20 bits from host_id_ptr, high 12 bits preserved
     *
     * Assembly reference:
     *   00e6910a    andi.l #-0x100000,(-0xa,A6)  ; Clear low 20 bits
     *   00e69118    or.l D0,(-0xa,A6)            ; OR in host_id
     *
     * The mask 0xFFF00000 keeps the high 12 bits.
     *
     * Note: In the original assembly, bytes 0-1 of the host field are
     * uninitialized stack data. We explicitly zero them for correctness.
     */
    source.host[0] = 0;
    source.host[1] = 0;

    /* Store host_id with only low 20 bits preserved in bytes 2-5 */
    {
        uint32_t host_masked = host_id & 0x000FFFFF;
        source.host[2] = (host_masked >> 24) & 0xFF;
        source.host[3] = (host_masked >> 16) & 0xFF;
        source.host[4] = (host_masked >> 8) & 0xFF;
        source.host[5] = host_masked & 0xFF;
    }

    /* Call RIP_$UPDATE_INT with flags=0 (standard routes) */
    RIP_$UPDATE_INT(*network_ptr, &source, *hop_count_ptr,
                    (uint8_t)port_index, 0, &status);

    *status_ret = status;
}
