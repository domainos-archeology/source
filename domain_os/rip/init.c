/*
 * RIP_$INIT - Initialize RIP routing subsystem
 *
 * This function initializes the RIP (Routing Information Protocol) subsystem.
 * It performs the following:
 *
 * 1. Initializes three exclusion locks:
 *    - RIP exclusion lock (at offset 0x40)
 *    - ROUTE service mutex (at offset 0x28)
 *    - XNS_ERROR client mutex (at offset 0x10)
 *
 * 2. If booting in diskless mode (NETWORK_$DISKLESS < 0):
 *    - Contacts the mother node to obtain initial routing information
 *    - Sets up the routing port (ROUTE_$PORT)
 *    - Initializes routing table entries via RIP_$UPDATE_INT
 *
 * Original address: 0x00E2FBD0
 */

#include "rip/rip_internal.h"
#include "sock/sock.h"
#include "pkt/pkt.h"
#include "netbuf/netbuf.h"
#include "app/app.h"

/*
 * Local structure for received packet info
 * Used when parsing responses from the mother node
 */
typedef struct app_recv_info_t {
    uint32_t    hdr_ptr;        /* 0x00: Header pointer */
    uint32_t    field_04;       /* 0x04: Field at offset 4 (packet length) */
    uint32_t    field_08;       /* 0x08: Field at offset 8 (packet ID) */
    uint32_t    data_ptr[4];    /* 0x0C: Data buffer pointers */
    uint32_t    response_val;   /* 0x1C: Response value (route port) */
    uint32_t    flags;          /* 0x20: Flags */
} app_recv_info_t;

/*
 * RIP_$INIT - Initialize RIP subsystem
 *
 * Called from NETWORK_$INIT during system initialization.
 */
void RIP_$INIT(void)
{
    uint8_t         local_bcast_ctrl[30];   /* Local copy of broadcast control */
    uint16_t        sock_num;               /* Allocated socket number */
    int8_t          result;                 /* Function return value */
    int32_t         ec_val;                 /* Event counter value */
    int32_t         wait_vals[3];           /* Wait values for EC_$WAIT */
    ec_$eventcount_t *wait_ecs[3];          /* Event counters for EC_$WAIT */
    int16_t         pkt_id;                 /* Packet ID */
    status_$t       status;                 /* Status return */
    uint16_t        timeout;                /* Timeout value */
    int32_t         deadline;               /* Response deadline */
    app_recv_info_t recv_info;              /* Received packet info */
    uint32_t        netbuf_info;            /* Network buffer info */
    int16_t         resp_pkt_id;            /* Response packet ID */
    uint16_t        resp_len;               /* Response length */
    uint32_t        route_port;             /* Received route port value */
    int16_t         wait_result;            /* EC_$WAIT result */
    ec_$eventcount_t *sock_ec;              /* Socket event count (A3/(A2-4)) */
    int             i;

    /*
     * Step 1: Initialize exclusion locks
     *
     * Three locks are initialized at different offsets in RIP_$DATA:
     * - RIP exclusion lock at +0x40 (main RIP lock)
     * - Route service mutex at +0x28
     * - XNS error client mutex at +0x10
     */
    ML_$EXCLUSION_INIT(&RIP_$DATA.exclusion);
    ML_$EXCLUSION_INIT(&RIP_$DATA.route_service_mutex);
    ML_$EXCLUSION_INIT(&RIP_$DATA.xns_error_mutex);

    /*
     * Step 2: Check if diskless boot
     *
     * NETWORK_$DISKLESS is negative (bit 7 set) if booting diskless.
     * In diskless mode, we need to contact the mother node to get
     * initial routing information.
     */
    if (NETWORK_$DISKLESS >= 0) {
        /* Not diskless - nothing more to do */
        return;
    }

    /*
     * Step 3: Diskless initialization
     *
     * Copy broadcast control parameters and clear the "local" flag (bit 7
     * of second byte) since we're requesting from the mother node.
     */
    for (i = 0; i < 30; i++) {
        local_bcast_ctrl[i] = RIP_$DATA.bcast_control[i];
    }
    local_bcast_ctrl[1] &= 0x7F;  /* Clear bit 7 (local flag) */

    /*
     * Step 4: Allocate a socket for the request
     *
     * SOCK_$ALLOCATE returns negative on success.
     * Parameters: socket_ret, flags (0x10001 = kernel mode), buffer_size (0x10400)
     */
    result = SOCK_$ALLOCATE(&sock_num, 0x10001, 0x10400);
    if (result >= 0) {
        /* Socket allocation failed */
        return;
    }

    /*
     * Step 5: Read socket's event counter and prepare for request
     *
     * Get the current event counter value so we can detect when
     * a response arrives.
     */
    /*
     * 0x00E2FC64 - 0x00E2FC82: the socket table is indexed from 0xE28DB4
     * with a -4 displacement, so the entry for socket n is
     * SOCK_$EVENT_COUNTERS[n - 1].
     */
    sock_ec = SOCK_$EVENT_COUNTERS[sock_num - 1];
    ec_val = EC_$READ(sock_ec);
    wait_vals[0] = ec_val + 1;  /* Wait for next event */

    /*
     * Step 6: Get a unique packet ID
     */
    pkt_id = PKT_$NEXT_ID();

    /*
     * Step 7: Send request to mother node
     *
     * Send a packet to the mother node (socket 1) requesting routing
     * information. The packet contains our broadcast control parameters.
     */
    PKT_$SEND_INTERNET(
        0,                      /* routing_key */
        NETWORK_$MOTHER_NODE,   /* dest_node */
        1,                      /* dest_sock (socket 1 = RIP) */
        0,                      /* src_node_or */
        NODE_$ME,               /* src_node */
        sock_num,               /* src_sock */
        local_bcast_ctrl,       /* pkt_info */
        pkt_id,                 /* request_id */
        NULL,                   /* template (none) */
        2,                      /* template_len (minimal) */
        NULL,                   /* data */
        0,                      /* data_len */
        &resp_len,              /* len_out */
        &timeout,               /* extra (timeout return) */
        &status                 /* status_ret */
    );

    if (status != status_$ok) {
        goto cleanup;
    }

    /*
     * Step 8: Wait for response
     *
     * Set up a timeout based on the returned timeout value and wait
     * for either a response or timeout.
     */
    /*
     * 0x00E2FCDA - 0x00E2FCF2: deadline = EC_$READ(&TIME_$CLOCKH) + timeout + 1.
     * Only the low 16 bits of the returned timeout are used ("andi.l
     * #0xFFFF,D3" at 0x00E2FCDA).
     */
    deadline = EC_$READ((ec_$eventcount_t *)&TIME_$CLOCKH) +
               (int32_t)(uint16_t)timeout + 1;
    wait_vals[1] = deadline;

    /*
     * 0x00E2FCF6: A3 is set to NULL before the wait, so the third slot of
     * both arrays is the terminator; its value slot still carries the 1
     * that "pea (0x1).w" pushes at 0x00E2FDA8.
     */
    wait_ecs[0] = sock_ec;
    wait_ecs[1] = (ec_$eventcount_t *)&TIME_$CLOCKH;
    wait_ecs[2] = NULL;  /* Terminate list */
    wait_vals[2] = 1;

    for (;;) {
        /*
         * 0x00E2FDA8 - 0x00E2FDD2.  Both arrays go by value; the result is
         * the 0-based index of the satisfied event count, so 0 means the
         * socket fired and anything else means the deadline passed
         * ("tst.w D0w / seq D3b / tst.b D3b / bmi" - receive only when the
         * result is zero).
         */
        wait_result = EC_$WAIT((ec_$wait_ecs_t){{ wait_ecs[0], wait_ecs[1],
                                                  wait_ecs[2] }},
                               (ec_$wait_vals_t){{ wait_vals[0], wait_vals[1],
                                                   wait_vals[2] }});

        if (wait_result != 0) {
            /* Deadline reached before a response arrived */
            goto cleanup;
        }

        /*
         * Receive packet from socket
         */
        APP_$RECEIVE(sock_num, &recv_info, &status);

        /*
         * Extract response info:
         * - resp_len: packet length from hdr_ptr + 4
         * - resp_pkt_id: packet ID from hdr_ptr + 6
         * - route_port: response value from recv_info.response_val
         */
        route_port = recv_info.response_val;
        resp_len = *(uint16_t *)((char *)recv_info.hdr_ptr + 4);
        resp_pkt_id = *(int16_t *)((char *)recv_info.hdr_ptr + 6);

        /*
         * Return the header buffer
         */
        netbuf_info = recv_info.flags & 0xFFFFFC00;
        NETBUF_$RTN_HDR(&netbuf_info);

        if (status != status_$ok) {
            goto cleanup;
        }

        /*
         * Dump any data buffers
         */
        if (recv_info.data_ptr[0] != 0) {
            PKT_$DUMP_DATA(recv_info.data_ptr, resp_len);
        }

        /*
         * Check if this is the response to our request
         */
        if (resp_pkt_id == pkt_id) {
            break;  /* Got our response */
        }

        /* Keep waiting for correct response */
    }

    /*
     * Step 9: Process response
     *
     * Store the route port from the response in both ROUTE_$PORT
     * and RIP_$DATA.route_port.
     */
    ROUTE_$PORT = route_port;
    RIP_$DATA.route_port = route_port;

    /*
     * Step 10: Initialize routing table entries
     *
     * Call RIP_$UPDATE_INT twice:
     * - First with flags = 0 (standard routes)
     * - Then with flags = 0xFF (non-standard routes)
     *
     * This sets up initial routing information from the mother node.
     */
    /*
     * 0x00E2FD6A: "movea.l #0xe26258,A0 / move.l D5,(A0) / ... / pea (A0)" -
     * the route port is stored at RIP_$DATA+0x00 and the SAME address is
     * then handed over as the 10-byte source address, i.e. the record's
     * first ten bytes are read as a rip_$xns_addr_t (network at +0x00, host
     * at +0x04).  The second call repeats it with the literal 0xE26258.
     */
    RIP_$UPDATE_INT(route_port, (rip_$xns_addr_t *)&RIP_$DATA, 0, 0, 0, &status);
    RIP_$UPDATE_INT(route_port, (rip_$xns_addr_t *)&RIP_$DATA, 0, 0, 0xFF, &status);

cleanup:
    /*
     * Step 11: Close the socket
     */
    SOCK_$CLOSE(sock_num);
}
