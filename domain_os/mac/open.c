/*
 * MAC_$OPEN - Open a MAC channel
 *
 * Opens a network channel on the specified port with the given packet
 * type filters.
 *
 * Original address: 0x00E0B8BE
 * Original size: 430 bytes
 */

#include "mac/mac_internal.h"
#include "route/route.h"
#include "sock/sock.h"
#include "mac_os/mac_os.h"

/*
 * The original function builds a complex stack frame for MAC_OS_$OPEN.
 * The frame includes:
 * - local_5c: status from MAC_OS_$OPEN
 * - local_58: channel number returned from MAC_OS_$OPEN
 * - local_c: pointer to MAC_$DEMUX callback
 * - local_8: number of packet types
 * - stack area for packet type min/max values
 *
 * The function also accesses:
 * - ROUTE_$PORT_ARRAY at 0xE2E0A0 (entry size 0x5C)
 * - the socket pointer table at 0xE28DB0 (SOCK_$SOCKET_PTR is its slot 1)
 * - MAC_OS_$CHANNEL_TABLE at A5+0x7A0 (A5 = 0xE22990, so 0xE23130)
 * - MAC_OS_$EXCLUSION at A5+0x868 (0xE231F8)
 */

void MAC_$OPEN(int16_t *port_num, mac_$open_params_t *params, status_$t *status_ret)
{
    uint16_t sock_num;
    status_$t os_status;
    uint32_t os_handle;
    uint16_t channel_num;
    int16_t port;
    int16_t num_types;
    int16_t i;
    uint32_t *type_ptr;
    uint8_t *sock_ptr;

    *status_ret = status_$ok;

    /* Validate port number (0-7) */
    port = *port_num;
    if (port < 0 || port > 7) {
        *status_ret = status_$mac_invalid_port;
        return;
    }

    /*
     * Check if port is open by examining port info table.
     * The port info at offset 0x2C contains the port type.
     * Types 0 and 1 (bits 0-1) indicate port is NOT available.
     * Original: if ((1 << (port_info[port].field_2c & 0x1f)) & 3) != 0) error
     */
    /*
     * 0x00E0B8F2:
     *   moveq  #0x5c,D1 ; movea.l #0xe2e0a0,A3 ; mulu.w D0w,D1
     *   moveq  #0x3,D3
     *   move.w (0x2c,A3,D1w*0x1),D2w
     *   btst.l D2,D3
     * 0xE2E0A0 with a 0x5C stride is ROUTE_$PORT_ARRAY, and +0x2C is
     * route_$port_t.active (asserted in route/route.h).  btst on a data
     * register takes the bit number modulo 32.
     */
    {
        uint16_t port_type = ROUTE_$PORT_ARRAY[port].active;
        if ((1u << (port_type & 0x1F)) & 3) {
            *status_ret = status_$internet_network_port_not_open;
            return;
        }
    }

    /* Validate packet type count (1-10) */
    num_types = params->num_packet_types;
    if (num_types < 1 || num_types > MAC_MAX_PACKET_TYPES) {
        *status_ret = status_$mac_invalid_packet_type_count;
        return;
    }

    /* Validate each packet type range (min <= max) */
    type_ptr = (uint32_t *)params->packet_types;
    for (i = num_types - 1; i >= 0; i--) {
        if (type_ptr[0] > type_ptr[1]) {
            *status_ret = status_$mac_invalid_packet_type;
            return;
        }
        type_ptr += 2;
    }

    /* Check that socket_count is non-zero */
    if (params->socket_count == 0) {
        *status_ret = status_$mac_no_socket_allocated;
        return;
    }

    /*
     * Allocate a user-mode socket.
     * SOCK_$ALLOCATE_USER returns negative on success.
     * Original passes socket_count for multiple parameters.
     */
    if (SOCK_$ALLOCATE_USER(&sock_num, params->socket_count,
                            params->socket_count, params->socket_count, 0x400) >= 0) {
        *status_ret = status_$mac_no_os_sockets_available;
        return;
    }

    /*
     * Clear bit 7 (0x80) of socket flags at offset 0x16.
     * This marks the socket for internal use until fully set up.
     * Original: bclr.b #0x7,(0x16,A3)
     */
    /*
     * 0x00E0B98E:
     *   movea.l #0xe28db4,A0
     *   move.w  D3w,D6w ; lsl.l #0x2,D6
     *   lea     (0x0,A0,D6*0x1),A1 ; move.l A1,D6
     *   movea.l (-0x4,A1),A3
     *   bclr.b  #0x7,(0x16,A3)
     * 0xE28DB4 indexed by sock_num*4 less 4 is slot sock_num of the socket
     * pointer table, i.e. SOCK_$SOCKET_PTR[sock_num - 1] (sock/sock.h).
     * The bclr is on the byte at descriptor offset 0x16.
     */
    sock_ptr = (uint8_t *)SOCK_$SOCKET_PTR[sock_num - 1];
    sock_ptr[0x16] &= 0x7F;

    /*
     * Build the frame and call MAC_OS_$OPEN.
     * The original code sets up:
     * - MAC_$DEMUX as callback at local_c
     * - num_packet_types at local_8
     * - Copies packet type min/max pairs to stack
     */
    /* The user-level mac_$open_params_t is passed straight through to
     * MAC_OS_$OPEN (declared with mac_os_$open_params_t *). */
    MAC_OS_$OPEN(port_num, (mac_os_$open_params_t *)params, &os_status);
    *status_ret = os_status;

    if (os_status != status_$ok) {
        /* Failed - close the socket we allocated */
        SOCK_$CLOSE(sock_num);
        return;
    }

    /*
     * MAC_OS_$OPEN returns channel_num in the params structure.
     * Extract it for local use.
     */
    channel_num = params->channel_num;
    os_handle = params->os_handle;

    /* Set process cleanup handler for MAC (cleanup type 0x0D) */
    PROC2_$SET_CLEANUP(0x0D);

    /* Enter exclusion region to update channel table */
    ML_$EXCLUSION_START(&MAC_OS_$EXCLUSION);

    /*
     * 0x00E0BA16:
     *   clr.l  D0 ; move.w D2w,D0w
     *   lsl.l  #0x2,D0 ; move.l D0,D1 ; lsl.l #0x2,D1 ; add.l D1,D0
     *   lea    (0x0,A5,D0*0x1),A0
     *   move.w D3w,(0x7a8,A0)
     * i.e. chan*4 + chan*16 = chan*20, then the socket word at A5+0x7A8.
     * MAC_OS_$CHANNEL_TABLE starts at A5+0x7A0 and .socket is at entry
     * offset 0x08, so 0x7A0 + 0x08 = 0x7A8 (mac_os/mac_os_data.c).
     */
    MAC_OS_$CHANNEL_TABLE[channel_num].socket = sock_num;

    /*
     * 0x00E0BA2A:
     *   move.b (0x54,A2),D1b ; lsr.b #0x7,D1b
     *   andi.b #-0x2,(0x7b2,A0) ; or.b D1b,(0x7b2,A0)
     * A5+0x7B2 is 0x7A0 + 0x12, the *high* byte of the .flags word, so bit 0
     * of that byte is bit 8 of the word: MAC_OS_CHANNEL_PROMISCUOUS.  The
     * read-modify-write is done on the whole word here so that it does not
     * depend on the byte order; the exclusion lock is held throughout.
     */
    {
        uint16_t flags = MAC_OS_$CHANNEL_TABLE[channel_num].flags;
        uint16_t promisc = (params->flags >> 7) & 1;

        flags &= (uint16_t)~MAC_OS_CHANNEL_PROMISCUOUS;
        flags |= (uint16_t)(promisc * MAC_OS_CHANNEL_PROMISCUOUS);
        MAC_OS_$CHANNEL_TABLE[channel_num].flags = flags;
    }

    ML_$EXCLUSION_STOP(&MAC_OS_$EXCLUSION);

    /*
     * Register the socket's EC1 with EC2 and return the handle.  D6 still
     * holds &SOCK table slot sock_num (0x00E0BA48: movea.l D6,A0 /
     * movea.l (-0x4,A0),A3), which is SOCK_$SOCKET_PTR[sock_num - 1].
     */
    sock_ptr = (uint8_t *)SOCK_$SOCKET_PTR[sock_num - 1];
    params->ec2_handle = EC2_$REGISTER_EC1((ec_$eventcount_t *)sock_ptr, status_ret);

    /* Store OS handle and channel number in params structure */
    *(uint32_t *)params = (uint32_t)(params->ec2_handle);  /* Already stored by EC2_$REGISTER_EC1 */
    ((uint32_t *)params)[1] = os_handle;
    ((uint16_t *)params)[4] = channel_num;
}
