/*
 * ROUTE_$SERVICE - Main routing service entry point
 *
 * This function handles various routing service operations controlled by
 * bit flags in the operation parameter:
 *
 *   - Bit 0 (0x01): Update network address
 *   - Bit 1 (0x02): Update port status
 *   - Bit 2 (0x04): Create new port (vs find existing)
 *   - Bit 3 (0x08): Close port operation
 *   - Bit 5 (0x20): User port with validation (queue length check)
 *
 * The function manages the lifecycle of routing ports including:
 *   - Creating new ports via NET_IO_$CREATE_PORT
 *   - Finding existing ports via ROUTE_$FIND_PORT
 *   - Updating RIP routing tables when networks change
 *   - Managing IDP channel registrations
 *   - Handling port status transitions
 *
 * Original address: 0x00E6A030
 *
 * Called from:
 *   - ROUTE_$READ_USER_STATS at 0x00E6A63E
 *   - Various XNS network functions
 */

#include "route/route_internal.h"
#include "rip/rip.h"
#include "rip/rip_internal.h"
#include "net_io/net_io.h"
#include "ml/ml.h"
#include "hint/hint.h"
#include "xns_idp/xns_idp.h"

/* NET_IO_$CREATE_PORT - declared in ring/ring_internal.h
 * Note: Previously couldn't include that header due to RING_$DATA type
 * conflicts, but that has been resolved by centralizing the declaration
 * in ring/ring.h. However, we still forward-declare here to avoid
 * pulling in unnecessary ring subsystem dependencies. */
int16_t NET_IO_$CREATE_PORT(int16_t port_type, uint16_t unit,
                            void *driver, uint16_t queue_length,
                            status_$t *status_ret);

/*
 * =============================================================================
 * Status Codes
 * =============================================================================
 */

#define status_$route_no_idp_channel        0x2B0001    /* IDP channel not initialized */
#define status_$route_invalid_port_status   0x2B0006    /* Invalid port status value */
#define status_$route_invalid_port_type     0x2B0009    /* Invalid port type */
#define status_$route_must_have_network     0x2B0011    /* Port must have network address */
#define status_$route_create_flag_required  0x2B0013    /* Create flag required for user ports */
#define status_$route_queue_length_too_large 0x2B0014   /* Queue length exceeds maximum */

/*
 * =============================================================================
 * Operation Flag Bits
 * =============================================================================
 */

#define SERVICE_OP_SET_NETWORK      0x01    /* Bit 0: Set network address */
#define SERVICE_OP_SET_STATUS       0x02    /* Bit 1: Set port status */
#define SERVICE_OP_CREATE_PORT      0x04    /* Bit 2: Create new port */
#define SERVICE_OP_CLOSE_PORT       0x08    /* Bit 3: Close port */
#define SERVICE_OP_USER_PORT        0x20    /* Bit 5: User port with validation */

/*
 * =============================================================================
 * Port Type and Status Masks
 * =============================================================================
 */

/* Port types 1 and 2 are valid - bits 1 and 2 = 0x06 */
#define PORT_TYPE_VALID_MASK        0x06

/* Status values 1,2,3,4,5 are valid - bits 1-5 = 0x3E */
#define PORT_STATUS_VALID_MASK      0x3E

/* Status bits 3,4,5 require network address - 0x38 */
#define PORT_STATUS_NEED_NETWORK    0x38

/* Status bits 2,3,4 for transition checks */
#define PORT_STATUS_ACTIVE_MASK     0x1C

/* Status bits 4,5 for routing enabled - 0x30 */
#define PORT_STATUS_ROUTING_MASK    0x30

/* Status bits 1,2,3 for routing disable check - 0x0E */
#define PORT_STATUS_DISABLE_STD     0x0E

/* Status bits 3,5 for N-routing check - 0x28 */
#define PORT_STATUS_N_ROUTING_MASK  0x28

/* Status bits 1,2,4 for N-routing disable - 0x16 */
#define PORT_STATUS_DISABLE_N       0x16

/* Maximum queue length for user ports */
#define MAX_USER_PORT_QUEUE_LENGTH  0x20

/*
 * =============================================================================
 * External Data
 * =============================================================================
 */

#if defined(ARCH_M68K)
    #define NET_IO_$NIL_DRIVER      ((void *)0xE244F4)
    #define NET_IO_$USER_DRIVER     ((void *)0xE24544)
    #define RIP_$STD_IDP_CHANNEL    (*(int16_t *)0xE26EBC)
    #define APP_$STD_IDP_CHANNEL    (*(int16_t *)0xE1DC20)
#else
    extern void *NET_IO_$NIL_DRIVER;
    extern void *NET_IO_$USER_DRIVER;
    extern int16_t RIP_$STD_IDP_CHANNEL;
    extern int16_t APP_$STD_IDP_CHANNEL;
#endif

/*
 * =============================================================================
 * Service Request Structure
 * =============================================================================
 */

/*
 * Service request structure passed as param_2.
 * Layout:
 *   +0x00: network address (4 bytes)
 *   +0x04: port status (2 bytes)
 *   +0x06: port type (2 bytes): 1=nil driver, 2=user driver
 *   +0x08: socket/network ID (2 bytes)
 *   +0x0A: queue length (2 bytes) - only for user ports
 */
typedef struct route_service_request_t {
    uint32_t    network;        /* 0x00: Network address */
    uint16_t    status;         /* 0x04: Port status */
    uint16_t    port_type;      /* 0x06: Port type (1=nil, 2=user) */
    int16_t     socket;         /* 0x08: Socket/network ID */
    uint16_t    queue_length;   /* 0x0A: Queue length for user ports */
} route_service_request_t;

/*
 * =============================================================================
 * Forward Declarations
 * =============================================================================
 *
 * Most functions are declared in the included headers:
 *   - ROUTE_$ANNOUNCE_NET in route/route_internal.h
 *   - HINT_$ADD_NET in hint/hint.h
 *   - NET_IO_$CREATE_PORT in net_io/net_io.h
 *   - RIP_$UPDATE_D in rip/rip.h
 *   - RIP_$SEND_UPDATES in rip/rip_internal.h
 */

/*
 * =============================================================================
 * RIP Operation Flags
 * =============================================================================
 *
 * These are passed as pointers to single-byte flags:
 *   - RIP_OP_ADD (0x00): Add route entry
 *   - RIP_OP_DELETE (0xFF): Delete route entry
 *
 * Original addresses:
 *   - DAT_00e69fae = 0x00 (add)
 *   - DAT_00e6a5da = 0xFF (delete)
 *   - DAT_00e69fb0 = 0x00 (add, alternate location)
 *   - DAT_00e6a5d8 = 0x0000 (hop count of 0)
 */
static const uint8_t RIP_OP_ADD = 0x00;
static const uint8_t RIP_OP_DELETE = 0xFF;
static const uint16_t RIP_HOP_COUNT_ZERO = 0x0000;

/*
 * =============================================================================
 * Implementation
 * =============================================================================
 */

/*
 * ROUTE_$SERVICE - Main service entry point
 *
 * @param operation_p   Pointer to operation flags structure (flags at offset +1)
 * @param request_p     Pointer to service request structure
 * @param status_ret    Output: status code
 */
void ROUTE_$SERVICE(void *operation_p, void *request_p, status_$t *status_ret)
{
    uint8_t *operation = (uint8_t *)operation_p;
    route_service_request_t *request = (route_service_request_t *)request_p;
    int16_t port_index;
    route_$port_t *port;
    route_$short_port_t short_port;
    uint32_t network_copy;
    uint16_t old_status;
    int16_t port_list[2];
    status_$t local_status;
    void *driver;
    uint16_t queue_length;
    uint8_t op_flags;

    /* Get operation flags from offset +1 */
    op_flags = operation[1];

    /* Initialize status to success */
    *status_ret = status_$ok;

    /* Acquire the service mutex */
    ML_$EXCLUSION_START(&ROUTE_$SERVICE_MUTEX);

    /*
     * Bit 3 (0x08): Close port operation
     * This is handled separately and returns early
     */
    if (op_flags & SERVICE_OP_CLOSE_PORT) {
        ROUTE_$CLOSE_PORT(request, status_ret);
        ML_$EXCLUSION_STOP(&ROUTE_$SERVICE_MUTEX);
        return;
    }

    /*
     * Bit 5 (0x20): User port validation
     * Requires port type 2 and create flag, with queue length <= 32
     */
    if (op_flags & SERVICE_OP_USER_PORT) {
        if (request->port_type != 2) {
            *status_ret = status_$route_invalid_port_type;
        } else if (!(op_flags & SERVICE_OP_CREATE_PORT)) {
            *status_ret = status_$route_create_flag_required;
        } else if (request->queue_length > MAX_USER_PORT_QUEUE_LENGTH) {
            *status_ret = status_$route_queue_length_too_large;
        }

        if (*status_ret != status_$ok) {
            ML_$EXCLUSION_STOP(&ROUTE_$SERVICE_MUTEX);
            return;
        }
    }

    /*
     * If port 0 has certain status bits set (0x3C = bits 2,3,4,5),
     * announce routing updates for port 0
     */
    if (((1 << (ROUTE_$PORT_ARRAY[0].active & 0x1f)) & 0x3C) != 0) {
        ROUTE_$SHORT_PORT(&ROUTE_$PORT_ARRAY[0], &short_port);
        network_copy = ROUTE_$PORT_ARRAY[0].network;

        /* Clear the short_port network fields for RIP update */
        short_port.host_id = 0;
        short_port.network2 = 0;
        short_port.socket = 0;

        /* Announce route additions to RIP */
        RIP_$UPDATE_D(&ROUTE_$PORT_ARRAY[0], &network_copy, &RIP_HOP_COUNT_ZERO,
                      &short_port, &RIP_OP_ADD, status_ret);
        RIP_$UPDATE_D(&ROUTE_$PORT_ARRAY[0], &network_copy, &RIP_HOP_COUNT_ZERO,
                      &short_port, &RIP_OP_DELETE, status_ret);
    }

    /*
     * Bit 2 (0x04): Create new port
     * If not set, find existing port by network/socket
     */
    if (op_flags & SERVICE_OP_CREATE_PORT) {
        /* Determine queue length */
        if (op_flags & SERVICE_OP_USER_PORT) {
            queue_length = request->queue_length;
        } else {
            queue_length = 10;  /* Default queue length */
        }

        /* Select driver based on port type */
        if (request->port_type == 1) {
            driver = NET_IO_$NIL_DRIVER;
        } else {
            driver = NET_IO_$USER_DRIVER;
        }

        /* Create the port */
        port_index = NET_IO_$CREATE_PORT(request->port_type, 0, driver,
                                          queue_length, status_ret);
        if (*status_ret != status_$ok) {
            ML_$EXCLUSION_STOP(&ROUTE_$SERVICE_MUTEX);
            return;
        }

        /* For user ports, increment counter and wire routing area */
        if (request->port_type == 2) {
            ROUTE_$N_USER_PORTS++;
            route_$wire_routing_area();
        }
    } else {
        /* Find existing port by network/socket */
        port_index = ROUTE_$FIND_PORT(request->port_type, (int32_t)request->socket);
        if (port_index == -1) {
            ML_$EXCLUSION_STOP(&ROUTE_$SERVICE_MUTEX);
            *status_ret = status_$internet_unknown_network_port;
            return;
        }
    }

    /*
     * Bit 1 (0x02): Set port status
     * Validate the new status value
     */
    if (op_flags & SERVICE_OP_SET_STATUS) {
        /* Status must be in valid range (bits 1-5 = values 1-5) */
        if (((1 << (request->status & 0x1f)) & PORT_STATUS_VALID_MASK) == 0) {
            ML_$EXCLUSION_STOP(&ROUTE_$SERVICE_MUTEX);
            *status_ret = status_$route_invalid_port_status;
            return;
        }
    }

    /* Get pointer to port structure */
    port = &ROUTE_$PORT_ARRAY[port_index];

    /*
     * Get the effective network address for validation
     * Use requested network if setting it, otherwise use current
     */
    uint32_t effective_network;
    if (op_flags & SERVICE_OP_SET_NETWORK) {
        effective_network = request->network;
    } else {
        effective_network = port->network;
    }

    /*
     * If network address is zero and status requires network,
     * return error (unless creating with specific status)
     */
    if (effective_network == 0) {
        uint16_t check_status;
        if (op_flags & SERVICE_OP_SET_STATUS) {
            check_status = request->status;
        } else {
            check_status = port->active;
        }

        /* Status bits 3,4,5 require non-zero network */
        if (((1 << (check_status & 0x1f)) & PORT_STATUS_NEED_NETWORK) != 0) {
            *status_ret = status_$route_must_have_network;
            ROUTE_$SHORT_PORT(port, (route_$short_port_t *)request);
            ML_$EXCLUSION_STOP(&ROUTE_$SERVICE_MUTEX);
            return;
        }
    }

    /*
     * Bit 0 (0x01): Set network address
     * Handle network address changes with RIP notifications
     */
    if ((op_flags & SERVICE_OP_SET_NETWORK) && (port->network != request->network)) {
        /* If port had a network address, remove old routes */
        if (port->network != 0) {
            ROUTE_$SHORT_PORT(port, &short_port);
            network_copy = port->network;

            /* Clear short_port network fields */
            short_port.host_id = 0;
            short_port.network2 = 0;
            short_port.socket = 0;

            /* Notify RIP of old route deletion */
            RIP_$UPDATE_D(port, &network_copy, (const uint8_t *)&RIP_HOP_COUNT_ZERO,
                          &short_port, &RIP_OP_ADD, &local_status);
            RIP_$UPDATE_D(port, &network_copy, (const uint8_t *)&RIP_HOP_COUNT_ZERO,
                          &short_port, &RIP_OP_DELETE, &local_status);
        }

        /* For port 0, announce network to mother and add hint */
        if (port_index == 0) {
            ROUTE_$ANNOUNCE_NET(request->network);
            HINT_$ADD_NET(request->network);
        }

        /* Update port's network address */
        port->network = request->network;
        /* Also update the cached copy at offset 0x20 */
        *(uint32_t *)((uint8_t *)port + 0x20) = request->network;

        /* If new network is non-zero, announce new routes */
        if (request->network != 0) {
            ROUTE_$SHORT_PORT(port, &short_port);
            network_copy = request->network;

            /* Clear short_port network fields */
            short_port.host_id = 0;
            short_port.network2 = 0;
            short_port.socket = 0;

            /* Notify RIP of new route addition */
            RIP_$UPDATE_D((route_$port_t *)request, &network_copy,
                          (const uint8_t *)&RIP_HOP_COUNT_ZERO,
                          &short_port, &RIP_OP_ADD, &local_status);
            RIP_$UPDATE_D((route_$port_t *)request, &network_copy,
                          (const uint8_t *)&RIP_HOP_COUNT_ZERO,
                          &short_port, &RIP_OP_DELETE, &local_status);
        }
    }

    /*
     * Bit 1 (0x02): Set port status
     * Handle status transitions with appropriate notifications
     */
    if (op_flags & SERVICE_OP_SET_STATUS) {
        old_status = port->active;

        if (old_status != request->status) {
            /*
             * Handle transition from routing states (4,5) to non-routing (1,2,3)
             * This decrements the STD routing counter
             */
            if (((1 << (old_status & 0x1f)) & PORT_STATUS_ROUTING_MASK) != 0 &&
                ((1 << (request->status & 0x1f)) & PORT_STATUS_DISABLE_STD) != 0) {
                ROUTE_$DECREMENT_PORT(0, port_index, 0xFF);  /* STD type */
            }

            /*
             * Handle transition from N-routing states (3,5) to non-N-routing (1,2,4)
             * This decrements the N routing counter
             */
            if (((1 << (old_status & 0x1f)) & PORT_STATUS_N_ROUTING_MASK) != 0 &&
                ((1 << (request->status & 0x1f)) & PORT_STATUS_DISABLE_N) != 0) {
                ROUTE_$DECREMENT_PORT(0, port_index, 0);  /* N type */
            }

            /*
             * Special handling for status 1 (inactive/nil)
             * Call driver's offline and detach callbacks
             */
            if (old_status == 1) {
                void *driver_info = *(void **)((uint8_t *)port + 0x48);
                void (*offline_callback)(route_$port_t *, status_$t *);
                void (*detach_callback)(route_$port_t *, void *, uint16_t, uint16_t);

                offline_callback = *(void (**)(route_$port_t *, status_$t *))
                                    ((uint8_t *)driver_info + 0x14);
                if (offline_callback != NULL) {
                    offline_callback((route_$port_t *)((uint8_t *)port + 0x30), status_ret);
                }

                if (*status_ret == status_$ok) {
                    detach_callback = *(void (**)(route_$port_t *, void *, uint16_t, uint16_t))
                                       ((uint8_t *)driver_info + 0x1C);
                    if (detach_callback != NULL) {
                        detach_callback((route_$port_t *)((uint8_t *)port + 0x30),
                                        NULL, 0, 0);
                    }
                }
            }

            /* Update the port status */
            port->active = request->status;

            /*
             * Special handling when transitioning TO status 1 with success
             * Register port with IDP channels
             */
            if (old_status == 1 && *status_ret == status_$ok) {
                if (RIP_$STD_IDP_CHANNEL != -1) {
                    port_list[0] = port_index;
                    XNS_IDP_$OS_ADD_PORT(&RIP_$STD_IDP_CHANNEL, port_list, &local_status);
                }
                if (APP_$STD_IDP_CHANNEL != -1) {
                    port_list[0] = port_index;
                    XNS_IDP_$OS_ADD_PORT(&APP_$STD_IDP_CHANNEL, port_list, &local_status);
                }
            }

            /*
             * Handle transition from non-routing (1,2,3) to routing states (4,5)
             * Initialize routing for this port
             */
            if (*status_ret == status_$ok &&
                ((1 << (old_status & 0x1f)) & PORT_STATUS_DISABLE_STD) != 0 &&
                ((1 << (request->status & 0x1f)) & PORT_STATUS_ROUTING_MASK) != 0) {
                if (RIP_$STD_IDP_CHANNEL == -1) {
                    *status_ret = status_$route_no_idp_channel;
                } else {
                    ROUTE_$INIT_ROUTING(port_index, 0xFF);  /* STD routing */
                }
            }

            /*
             * Handle transition from non-N-routing (1,2,4) to N-routing states (3,5)
             * Initialize N-routing for this port
             */
            if (*status_ret == status_$ok &&
                ((1 << (old_status & 0x1f)) & PORT_STATUS_DISABLE_N) != 0 &&
                ((1 << (request->status & 0x1f)) & PORT_STATUS_N_ROUTING_MASK) != 0) {
                ROUTE_$INIT_ROUTING(port_index, 0);  /* N routing */
            }

            /*
             * On success, if new status is 1 (nil/inactive), clean up
             */
            if (*status_ret == status_$ok && port->active == 1) {
                void *driver_info = *(void **)((uint8_t *)port + 0x48);
                void (*online_callback)(route_$port_t *, status_$t *);

                online_callback = *(void (**)(route_$port_t *, status_$t *))
                                   ((uint8_t *)driver_info + 0x18);
                if (online_callback != NULL) {
                    online_callback((route_$port_t *)((uint8_t *)port + 0x30), status_ret);
                }

                /* Unregister from IDP channels */
                if (RIP_$STD_IDP_CHANNEL != -1) {
                    port_list[0] = port_index;
                    XNS_IDP_$OS_DELETE_PORT(&RIP_$STD_IDP_CHANNEL, port_list, &local_status);
                }
                if (APP_$STD_IDP_CHANNEL != -1) {
                    port_list[0] = port_index;
                    XNS_IDP_$OS_DELETE_PORT(&APP_$STD_IDP_CHANNEL, port_list, &local_status);
                }
            } else {
                /* On failure, restore original status */
                port->active = old_status;
            }
        }
    }

    /* Release the mutex */
    ML_$EXCLUSION_STOP(&ROUTE_$SERVICE_MUTEX);

    /* Send RIP updates for both routing types */
    RIP_$SEND_UPDATES(0);           /* Type 0 updates */
    RIP_$SEND_UPDATES(0xFF);        /* Type 0xFF updates */

    /* Copy port info back to request structure */
    ROUTE_$SHORT_PORT(port, (route_$short_port_t *)request);
}
