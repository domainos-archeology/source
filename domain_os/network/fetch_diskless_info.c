/*
 * network_$fetch_diskless_info - Fetch info from network for diskless boot
 *
 * Queries ASKNODE_$INTERNET_INFO for node-specific data and processes
 * the result based on the command type:
 *
 *   cmd=2:  Update TIME_$CLOCKH from remote node's clock
 *   cmd=8:  Update CAL_$TIMEZONE (UTC delta, tz name, drift)
 *   cmd=0x37: Update routing table if route port changed:
 *             - Set up UID with node address
 *             - Call HINT_$ADDI to register hint
 *             - Call RIP_$UPDATE_INT to update routing
 *
 * On error for cmd != 0x37: calls CRASH_SYSTEM.
 * cmd=0x37 tolerates errors gracefully (network may not be available).
 *
 * Parameters:
 *   cmd   - Command type (2=time, 8=timezone, 0x37=routing)
 *   param - Network node address (NETWORK_$MOTHER_NODE typically)
 *
 * Original address: 0x00E3366C
 * Size: 262 bytes
 *
 * TODO: Full implementation requires ASKNODE_$INTERNET_INFO response
 * structure layout and CAL_$TIMEZONE field offsets.
 */

#include "os/os_internal.h"

/* Stub - 262-byte diskless info fetch with 3 command types */
