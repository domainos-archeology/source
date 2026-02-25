/*
 * network_$fetch_diskless_info - Fetch info from network for diskless boot
 *
 * Queries ASKNODE_$INTERNET_INFO for node-specific data and processes
 * the result based on the command type:
 *
 *   cmd=2  (ASKNODE_REQ_BOOT_TIME): Update TIME_$CLOCKH from remote node's clock
 *   cmd=8  (ASKNODE_REQ_TIMEZONE):  Update CAL_$TIMEZONE (12-byte copy of timezone record)
 *   cmd=0x37: Update routing table if route port changed:
 *             - Build UID with node address in low 20 bits
 *             - Call HINT_$ADDI to register hint
 *             - Build XNS source address and call RIP_$UPDATE_INT
 *               with hop_count=1, port_index=0, flags=0
 *
 * On error for cmd != 0x37: calls CRASH_SYSTEM.
 * cmd=0x37 tolerates errors gracefully (network may not be available).
 *
 * Parameters:
 *   cmd   - Command type (2=time, 8=timezone, 0x37=routing)
 *   node  - Network node address (NETWORK_$MOTHER_NODE typically)
 *
 * Original address: 0x00E3366C
 * Size: 262 bytes
 */

#include "network/network_internal.h"
#include "asknode/asknode.h"
#include "cal/cal.h"
#include "hint/hint.h"
#include "misc/misc.h"
#include "rip/rip_internal.h"
#include "route/route.h"
#include "time/time.h"

void network_$fetch_diskless_info(int16_t cmd, uint32_t node)
{
    status_$t status;

    /*
     * Response buffer for ASKNODE_$INTERNET_INFO.
     * Layout varies by cmd but has a common pattern:
     *   +0x00: first response word (not used directly here)
     *   +0x04: response status code
     *   +0x08: command-specific data begins
     */
    uint32_t result[6]; /* 24 bytes - enough for timezone (12 bytes at offset 8) */

    /*
     * Constants passed by reference to ASKNODE_$INTERNET_INFO.
     * In the original code, these are PC-relative read-only data
     * at 0xE33772 (resp_len = 0x0200) and 0xE33774 (req_len = 0, param = nil).
     */
    uint16_t resp_len = 0x0200;  /* 512 bytes max response */
    int32_t req_len = 0;         /* no extra request data */
    uid_t param = { 0, 0 };      /* nil UID */

    /* Query the specified node for the requested information */
    ASKNODE_$INTERNET_INFO(
        (uint16_t *)&cmd,   /* request type */
        &node,              /* target node ID */
        &req_len,           /* request length = 0 */
        &param,             /* request param = nil UID */
        &resp_len,          /* response length limit = 512 */
        result,             /* result buffer */
        &status             /* output status */
    );

    /*
     * Error handling:
     *   1. Check ASKNODE call status
     *   2. If OK, check response status embedded at result[1] (offset 4)
     *   3. For cmd != 0x37, any error is fatal (CRASH_SYSTEM)
     *   4. cmd=0x37 tolerates errors (diskless boot may not have network)
     */
    if (status != status_$ok || (status = (status_$t)result[1], status != status_$ok)) {
        if (cmd != 0x37) {
            CRASH_SYSTEM(&status);
        }
    }

    switch (cmd) {

    case ASKNODE_REQ_BOOT_TIME: /* 0x02 */
        /*
         * result[3] (offset 12) contains the remote node's clock high word.
         * Store it as TIME_$CLOCKH for time synchronization during diskless boot.
         */
        TIME_$CLOCKH = result[3];
        break;

    case ASKNODE_REQ_TIMEZONE: /* 0x08 */
        /*
         * result[2..4] (offset 8, 12 bytes) contains the cal_$timezone_rec_t
         * from the remote node. Copy it directly into CAL_$TIMEZONE.
         *
         * Assembly uses 3 longword moves: (A0)+→(A1)+, (A0)+→(A1)+, (A0)+→(A1)+
         * from result+8 to CAL_$TIMEZONE (12 bytes = sizeof(cal_$timezone_rec_t)
         * without boot_volx).
         */
        {
            uint32_t *src = &result[2];
            uint32_t *dst = (uint32_t *)&CAL_$TIMEZONE;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
        }
        break;

    case 0x37: /* Routing update */
        /*
         * Only process if status is OK and the route port has changed.
         * result[2] (offset 8) contains the remote node's route port.
         */
        if (status == status_$ok && ROUTE_$PORT != result[2]) {
            uint32_t response_port = result[2];

            /*
             * Build a UID with UID_$NIL as the base, setting the low 20 bits
             * of uid.low to the node address. Since UID_$NIL is all zeros,
             * this effectively sets uid = {0, node}.
             */
            uid_t local_uid;
            local_uid.high = UID_$NIL.high;
            local_uid.low = (UID_$NIL.low & 0xFFF00000) | node;

            /*
             * Build hint data: [route_port, node_address]
             * HINT_$ADDI registers this UID → location mapping.
             */
            uint32_t hint_data[2];
            hint_data[0] = response_port;
            hint_data[1] = node;

            HINT_$ADDI(&local_uid, hint_data);

            /*
             * Build an XNS source address for the routing update.
             * Set network to the response port. For the host address,
             * set the low 20 bits to the node address.
             *
             * Note: In the original assembly, the first 2 bytes of host[]
             * are uninitialized stack memory. Only host[2..5] (the last 4
             * bytes, treated as a uint32_t) are explicitly set via AND/OR.
             */
            rip_$xns_addr_t source;
            source.network = response_port;
            /* Initialize host bytes that the original code leaves unset */
            source.host[0] = 0;
            source.host[1] = 0;
            /*
             * Original assembly: andi.l #0xFFF00000,(-0xa,A6) / or.l D1,(-0xa,A6)
             * This operates on host[2..5] as a 32-bit value.
             * Since the stack is uninitialized, the AND preserves whatever
             * was in the top 12 bits. We zero them for portability.
             */
            {
                uint32_t host_low;
                host_low = node; /* & 0x000FFFFF implicit: node is 20-bit */
                source.host[2] = (uint8_t)(host_low >> 24);
                source.host[3] = (uint8_t)(host_low >> 16);
                source.host[4] = (uint8_t)(host_low >> 8);
                source.host[5] = (uint8_t)(host_low);
            }

            /*
             * Update routing table: add a direct route (hop_count=1)
             * to the response_port network via our source address.
             * port_index=0, flags=0 (standard routes).
             */
            RIP_$UPDATE_INT(response_port, &source, 1, 0, 0, &status);
        }
        break;

    default:
        /* Other command types: no action */
        break;
    }
}
