/*
 * MSG_$FREE_ASID - Free ASID resources
 *
 * Closes all sockets owned by the specified ASID.
 * Called during process cleanup.
 *
 * Original address: 0x00E74E2C
 * Original size: 86 bytes
 */

#include "msg/msg_internal.h"

void MSG_$FREE_ASID(uint16_t *asid_ptr)
{
#if defined(M68K)
    int16_t sock_num;
    uint8_t asid;
    uint8_t byte_index;
    uint8_t *bitmap;
    msg_$socket_t socket;
    status_$t status;

    asid = (uint8_t)*asid_ptr;

    /*
     * Iterate through all sockets (1-223).
     * Close any socket owned by this ASID.
     */
    for (sock_num = 1; sock_num < MSG_MAX_SOCKET; sock_num++) {
        bitmap = (uint8_t *)(MSG_$DATA_BASE + MSG_OFF_OWNERSHIP + sock_num * 8);

        /* Check if this ASID owns the socket */
        byte_index = (0x3F - asid) >> 3;
        if ((bitmap[byte_index] & (1 << (asid & 7))) != 0) {
            /* Close this socket */
            socket = sock_num;
            MSG_$CLOSEI(&socket, &status);
        }
    }

#else
    (void)asid_ptr;
#endif
}
