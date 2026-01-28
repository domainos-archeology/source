/*
 * ROUTE_$INIT_ROUTING - Initialize routing subsystem
 *
 * This function is called when routing is being enabled on a port.
 * It increments the appropriate port counter (STD or N) and when the
 * total routing ports reaches 2 (with one counter still at 1), it
 * initializes the entire routing subsystem.
 *
 * Initialization includes:
 *   - Clearing the routing data area (0x81 longs at 0xe87da8)
 *   - Initializing the control event counter
 *   - Creating the ROUTE_$PROCESS server process
 *   - Allocating a socket for routing communication
 *   - Setting up socket event counters
 *   - Clearing statistics counters
 *
 * @param port_index   Port index (0-7) being initialized
 * @param port_type    Port type flag: negative = increment STD counter,
 *                     non-negative = increment N counter
 *
 * Original address: 0x00E69CCC
 *
 * Called from:
 *   - ROUTE_$SERVICE at 0x00E6A4DC (STD routing port, port_type=0xFF)
 *   - ROUTE_$SERVICE at 0x00E6A512 (normal routing port, port_type=0x00)
 */

#include "route/route_internal.h"
#include "proc1/proc1.h"
#include "sock/sock.h"
#include "time/time.h"
#include "misc/crash_system.h"

/* Process creation flags */
#define PROC_FLAG_ROUTING       0x1000000C

/* Socket allocation parameters */
#define SOCK_ALLOC_TYPE         0x400040
#define SOCK_ALLOC_SIZE         0x400400

/*
 * Routing data area cleared during initialization (0xe87da8)
 * Size: 0x81 longs = 516 bytes
 */
#if defined(ARCH_M68K)
#define ROUTE_$DATA_AREA        ((uint32_t *)0xE87DA8)
#define ROUTE_$PROCESS_UID      (*(uint16_t *)0xE88216)
#define ROUTE_$USER_PORT_MAX    (*(uint16_t *)0xE87FD0)
#define ROUTE_$USER_PORT_COUNT  (*(uint32_t *)0xE87FCC)
#define ROUTE_$STAT0            (*(uint32_t *)0xE87FC0)
#define ROUTE_$STAT1            (*(uint32_t *)0xE87FC4)
#define ROUTE_$STAT2            (*(uint32_t *)0xE87FC8)
#define ROUTE_$STAT3            (*(uint32_t *)0xE87FBC)
#define ROUTE_$STAT4            (*(uint32_t *)0xE87FB0)
#define ROUTE_$STAT5            (*(uint32_t *)0xE87FB4)
#define ROUTE_$STAT6            (*(uint32_t *)0xE87FB8)
#define ROUTE_$STAT7            (*(uint32_t *)0xE87FAC)
#define ROUTE_$LAST_UPDATE_TIME (*(uint32_t *)0xE825DC)
#define SOCK_$EVENT_COUNTERS    ((ec_$eventcount_t **)0xE28DB4)
#else
extern uint32_t ROUTE_$DATA_AREA[];
extern uint16_t ROUTE_$PROCESS_UID;
extern uint16_t ROUTE_$USER_PORT_MAX;
extern uint32_t ROUTE_$USER_PORT_COUNT;
extern uint32_t ROUTE_$STAT0;
extern uint32_t ROUTE_$STAT1;
extern uint32_t ROUTE_$STAT2;
extern uint32_t ROUTE_$STAT3;
extern uint32_t ROUTE_$STAT4;
extern uint32_t ROUTE_$STAT5;
extern uint32_t ROUTE_$STAT6;
extern uint32_t ROUTE_$STAT7;
extern uint32_t ROUTE_$LAST_UPDATE_TIME;
extern ec_$eventcount_t *SOCK_$EVENT_COUNTERS[];
#endif

/* Forward declaration of routing process entry point */
void ROUTE_$PROCESS(void);

/* Forward declaration of helper function at 0xe69bce */
static void route_$update_port_count(void);

/* Error status for failed socket allocation */
static const status_$t status_$route_sock_alloc_failed = 0x2B0015;

void ROUTE_$INIT_ROUTING(int16_t port_index, int8_t port_type)
{
    int16_t std_count, n_count;
    int32_t i;
    uint32_t *data_ptr;
    uint16_t socket;
    ec_$eventcount_t *ec;
    int32_t ec_val;
    status_$t status;
    int8_t result;

    /*
     * Increment the appropriate counter based on port_type:
     *   - Negative (0xFF): STD routing port (standard network)
     *   - Non-negative (0x00): Normal routing port
     */
    if (port_type < 0) {
        ROUTE_$STD_N_ROUTING_PORTS++;
        std_count = ROUTE_$STD_N_ROUTING_PORTS;
        n_count = ROUTE_$N_ROUTING_PORTS;
    } else {
        ROUTE_$N_ROUTING_PORTS++;
        std_count = ROUTE_$STD_N_ROUTING_PORTS;
        n_count = ROUTE_$N_ROUTING_PORTS;
    }

    /*
     * Initialize routing subsystem only when:
     *   - Total routing ports reaches 2
     *   - One counter is at 2 and the other is less than 2
     * This ensures initialization happens once when the first
     * two routing ports are configured (one of each type).
     */
    if (n_count == 2 && std_count < 2) {
        /* Condition met - proceed with initialization */
    } else if (std_count == 2 && n_count < 2) {
        /* Condition met - proceed with initialization */
    } else {
        /* Not time to initialize yet */
        return;
    }

    /*
     * Clear routing data area (0x81 longs = 516 bytes)
     * This area contains routing tables and working data.
     */
    data_ptr = ROUTE_$DATA_AREA;
    for (i = 0x80; i >= 0; i--) {
        *data_ptr++ = 0;
    }

    /*
     * Initialize control event counter
     * Used to synchronize the routing process
     */
    EC_$INIT((ec_$eventcount_t *)&ROUTE_$CONTROL_EC);
    ec_val = EC_$READ((ec_$eventcount_t *)&ROUTE_$CONTROL_EC);
    ROUTE_$CONTROL_ECVAL = ec_val + 1;

    /*
     * Create the routing process
     * This process runs ROUTE_$PROCESS to handle routing updates
     */
    ROUTE_$PROCESS_UID = PROC1_$CREATE_P((void *)ROUTE_$PROCESS,
                                          PROC_FLAG_ROUTING,
                                          &status);

    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
        return;
    }

    /*
     * Update port count tracking (helper function at 0xe69bce)
     */
    route_$update_port_count();

    /*
     * Set maximum user ports
     */
    ROUTE_$USER_PORT_MAX = 0x40;

    /*
     * Allocate a socket for routing communication
     * SOCK_$ALLOCATE returns negative on success (Apollo convention)
     */
    result = SOCK_$ALLOCATE(&socket, SOCK_ALLOC_TYPE, SOCK_ALLOC_SIZE);
    if (result >= 0) {
        /* Allocation failed - crash */
        CRASH_SYSTEM(&status_$route_sock_alloc_failed);
        return;
    }

    /*
     * Get the event counter for this socket and clear the high bit
     * of the waiter flags (offset 0x16 in ec structure).
     * The -4 offset accesses the previous array entry due to how
     * the original code indexes.
     */
    ec = SOCK_$EVENT_COUNTERS[socket];
    /* Clear bit 7 of byte at offset 0x16 in ec structure */
    /* This corresponds to (ec + 1)->waiter_list_tail + 2 in original */
    ((uint8_t *)ec)[0x16] &= 0x7F;

    /*
     * Read and store socket event counter value
     */
    ec_val = EC_$READ(ec);
    ROUTE_$SOCK_ECVAL = ec_val + 1;
    ROUTE_$SOCK = socket;

    /*
     * Store current time for last update tracking
     * A5 register in original points to ROUTE_$LAST_UPDATE_TIME
     */
    ROUTE_$LAST_UPDATE_TIME = TIME_$CURRENT_CLOCKH;

    /*
     * Clear all statistics counters
     */
    ROUTE_$USER_PORT_COUNT = 0;
    ROUTE_$STAT0 = 0;
    ROUTE_$STAT1 = 0;
    ROUTE_$STAT2 = 0;
    ROUTE_$STAT3 = 0;
    ROUTE_$STAT4 = 0;
    ROUTE_$STAT5 = 0;
    ROUTE_$STAT6 = 0;
    ROUTE_$STAT7 = 0;

    /*
     * Advance control EC to signal process startup complete
     */
    EC_$ADVANCE((ec_$eventcount_t *)&ROUTE_$CONTROL_EC);
}

/*
 * route_$update_port_count - Update port count tracking
 *
 * Helper function at 0xe69bce that updates internal port count
 * tracking. Called during initialization and when ports are
 * added/removed.
 *
 * TODO: Implement fully after analyzing 0xe69bce
 */
static void route_$update_port_count(void)
{
    /* Stub - needs implementation based on analysis of 0xe69bce */
    /* This function updates DAT_00e87fd4 and related counters */
}
