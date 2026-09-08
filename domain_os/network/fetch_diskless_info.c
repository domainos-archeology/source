/*
 * network_$fetch_diskless_info - ask another node for boot-time information
 *
 * Sends one ASKNODE_$INTERNET_INFO request and folds the answer into local
 * state: the clock for request 2, the timezone record for request 8, and a
 * hint plus a routing entry for request 0x37.  Any error on a request other
 * than 0x37 crashes the system.
 *
 * Original address: 0x00E3366C, size 262 bytes (0x00E3366C-0x00E33771).
 *
 * Frame map (link A6,-0x224):
 *   A6-0x224  status
 *   A6-0x220  the 8-byte UID handed to HINT_$ADDI
 *   A6-0x218  the 8-byte hint value {network, node}
 *   A6-0x210  the 0x200-byte ASKNODE response buffer
 *   A6-0x010  the 10-byte rip_$xns_addr_t handed to RIP_$UPDATE_INT
 */

#include "network/network_internal.h"
#include "asknode/asknode.h"
#include "cal/cal.h"
#include "hint/hint.h"
#include "misc/misc.h"
#include "rip/rip.h"
#include "route/route.h"
#include "time/time.h"
#include "uid/uid.h"

/*
 * The response buffer ASKNODE_$INTERNET_INFO is given, and the length the
 * caller declares for it.  0x200 is the word at 0x00E33772, the "pea (0xf2,PC)"
 * operand at 0x00E3367E:
 *   00e33770  4e 75 02 00 00 00 00 00
 */
#define NETWORK_DISKLESS_RESP_SIZE  0x200

/*
 * network_$diskless_resp_len - the constant cell at 0x00E33772 (word 0x0200).
 */
static const uint16_t network_$diskless_resp_len = NETWORK_DISKLESS_RESP_SIZE;

/*
 * network_$diskless_req_zero - the constant cell at 0x00E33774, four bytes of
 * zero, which the image passes as BOTH the req_len (argument 3) and the param
 * (argument 4): "pea (0xf0,PC)" pushes it once and "move.l (SP),-(SP)"
 * duplicates the pointer.  Only four bytes exist there - 0x00E33778 is already
 * the next routine's "link" - so the uid_t argument is a four-byte cell in
 * this call, and it is const because ASKNODE never writes it.
 */
static const int32_t network_$diskless_req_zero = 0;

void network_$fetch_diskless_info(int16_t cmd, uint32_t node)
{
    status_$t   status;                                     /* A6-0x224 */
    uid_t       hint_uid;                                   /* A6-0x220 */
    uint32_t    hint_value[2];                              /* A6-0x218 */
    uint32_t    result[NETWORK_DISKLESS_RESP_SIZE / 4];     /* A6-0x210 */

    /*
     * 0x00E33676-0x00E33696: seven by-reference arguments and no result slot
     * ("lea (0x1c,SP),SP").
     */
    ASKNODE_$INTERNET_INFO((uint16_t *)&cmd,
                           &node,
                           (int32_t *)&network_$diskless_req_zero,
                           (uid_t *)&network_$diskless_req_zero,
                           (uint16_t *)&network_$diskless_resp_len,
                           result,
                           &status);

    /*
     * 0x00E3369A-0x00E336B8.  The call's own status is tested first; when it
     * is clean the reply's status (result + 0x04) replaces it.  Request 0x37
     * tolerates a failure, everything else crashes.
     */
    if (status != status_$ok ||
        (status = (status_$t)result[1], status != status_$ok)) {
        if (cmd != ASKNODE_REQ_ROUTE_PORT) {
            CRASH_SYSTEM(&status);
        }
    }

    /* 0x00E336BA-0x00E336CE: an explicit three-way compare, not a table */
    switch (cmd) {

    case ASKNODE_REQ_BOOT_TIME:     /* 0x02 */
        /* 0x00E336D2: move.l (-0x204,A6),(0x00e2b0d4).l - reply + 0x0C */
        TIME_$CLOCKH = result[3];
        break;

    case ASKNODE_REQ_TIMEZONE:      /* 0x08 */
        /*
         * 0x00E336DE-0x00E336EE: twelve bytes from reply + 0x08 into
         * CAL_$TIMEZONE (0x00E7B030), as three longword moves.
         */
        {
            uint32_t *dst = (uint32_t *)&CAL_$TIMEZONE;

            dst[0] = result[2];
            dst[1] = result[3];
            dst[2] = result[4];
        }
        break;

    case ASKNODE_REQ_ROUTE_PORT:       /* 0x37 */
        /*
         * 0x00E336F0-0x00E33700: only a clean reply counts, and only when the
         * network the remote node named differs from ours.
         */
        if (status == status_$ok && ROUTE_$PORT != result[2]) {
            uint32_t reply_network = result[2];         /* reply + 0x08 */
            /*
             * A6-0x10.  The union gives the record the two views the image
             * uses: RIP_$UPDATE_INT is declared to take a rip_$xns_addr_t,
             * while the two stores below are a longword at +0x00 and a masked
             * longword at +0x06, which is the rip_$nexthop_t spelling.
             */
            union {
                rip_$xns_addr_t xns;
                rip_$nexthop_t  fields;
            } source;

            /*
             * 0x00E33702-0x00E3371C: UID_$NIL with its low twenty bits
             * replaced by the node id.
             */
            hint_uid = UID_$NIL;
            hint_uid.low = (hint_uid.low & 0xFFF00000u) | node;

            /* 0x00E33720 / 0x00E33724 */
            hint_value[1] = node;
            hint_value[0] = reply_network;

            /* 0x00E3372A-0x00E33738 */
            HINT_$ADDI(&hint_uid, hint_value);

            /*
             * 0x00E3373A-0x00E3374E: the routing source address.  Only
             * source.network and the low twenty bits of the longword at
             * source + 0x06 are written; source + 0x04 and the top twelve bits
             * of source + 0x06 keep whatever the stack held.  Spelled as a
             * rip_$nexthop_t so the andi/or land on a longword the way the
             * image's do, rather than on six separate bytes.
             */
            source.fields.network = reply_network;
            source.fields.host_lo = (source.fields.host_lo & 0xFFF00000u) | node;

            /*
             * 0x00E33750-0x00E33768: five arguments plus a word result slot,
             * and the image never pops them (unlk restores SP).
             */
            RIP_$UPDATE_INT(reply_network, &source.xns, 1, 0, 0, &status);
        }
        break;

    default:
        /* 0x00E336CE: every other request type does nothing further */
        break;
    }
}
