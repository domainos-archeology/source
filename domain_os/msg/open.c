/*
 * MSG_$OPEN, MSG_$OPENI - Open a message socket
 *
 * Opens a message socket for the current process.
 * The socket must not already be in use.
 *
 * Original addresses:
 *   MSG_$OPEN:  0x00E59198 (28 bytes)
 *   MSG_$OPENI: 0x00E591B4 (276 bytes)
 */

#include "msg/msg_internal.h"

/*
 * Network service callback address for MSG.
 * Original: DAT_00e592c8 (points to network service handler)
 */
extern void MSG_$NET_SERVICE(void);

/*
 * MSG_$OPENI - Open socket internal implementation
 *
 * Parameters:
 *   socket - Pointer to socket number to open
 *   depth  - Pointer to socket depth (max messages queued)
 *   status_ret - Status return
 */
void MSG_$OPENI(msg_$socket_t *socket, int16_t *depth, status_$t *status_ret)
{
#if defined(M68K)
    int16_t sock_num;
    int16_t sock_depth;
    int16_t sock_offset;
    uint8_t asid;
    uint8_t ownership[8];
    uint8_t byte_index;
    uint8_t *bitmap;
    status_$t net_status;
    uint32_t service_type;

    sock_num = *socket;
    sock_depth = *depth;

    /* Validate socket number (1-223) */
    if (sock_num < 1 || sock_num >= MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;
        return;
    }

    /* Validate depth (max 32) */
    if (sock_depth > MSG_MAX_DEPTH) {
        *status_ret = status_$msg_too_deep;
        return;
    }

    /* Lock the socket table */
    ML_$EXCLUSION_START((void *)MSG_$SOCK_LOCK);

    /* Calculate offset into ownership table: socket * 8 */
    sock_offset = sock_num << 3;

    /* Get pointer to ownership bitmap for this socket */
    bitmap = (uint8_t *)(MSG_$DATA_BASE + MSG_OFF_OWNERSHIP + sock_offset);

    /*
     * Check if socket is free (all ownership bits clear).
     * Ownership is stored as two 32-bit words (8 bytes total).
     */
    if (*(uint32_t *)bitmap != 0 || *(uint32_t *)(bitmap + 4) != 0) {
        *status_ret = status_$msg_socket_in_use;
        ML_$EXCLUSION_STOP((void *)MSG_$SOCK_LOCK);
        return;
    }

    /*
     * Try to open the underlying socket.
     * SOCK_$OPEN returns negative on success.
     */
    if (SOCK_$OPEN(sock_num, sock_depth, sock_depth, 0x0400) >= 0) {
        *status_ret = status_$msg_socket_in_use;
        ML_$EXCLUSION_STOP((void *)MSG_$SOCK_LOCK);
        return;
    }

    /*
     * Build ownership bitmap with current ASID's bit set.
     * The bitmap uses inverted ASID indexing: byte_index = (0x3F - ASID) >> 3
     */
    asid = PROC1_$AS_ID;

    ownership[0] = 0;
    ownership[1] = 0;
    ownership[2] = 0;
    ownership[3] = 0;
    ownership[4] = 0;
    ownership[5] = 0;
    ownership[6] = 0;
    ownership[7] = 0;

    byte_index = (0x3F - asid) >> 3;
    ownership[byte_index] |= (1 << (asid & 7));

    /* Store ownership bitmap */
    *(uint32_t *)bitmap = *(uint32_t *)ownership;
    *(uint32_t *)(bitmap + 4) = *(uint32_t *)(ownership + 4);

    /* Store socket depth */
    *(int16_t *)(MSG_$DATA_BASE + MSG_OFF_DEPTH_TABLE + sock_num * 2) = sock_depth;

    /* Increment open socket count */
    (*(int16_t *)(MSG_$DATA_BASE + MSG_OFF_OPEN_COUNT))++;

    /* Register process cleanup handler (type 7 = MSG cleanup) */
    PROC2_$SET_CLEANUP(7);

    /* Register network service for message handling */
    service_type = 0x80000;  /* Service type identifier */
    NETWORK_$SET_SERVICE(MSG_$NET_SERVICE, &service_type, &net_status);

    /* Mark that user sockets are open */
    *(uint8_t *)0xE24C48 = 0xFF;  /* NETWORK_$USER_SOCK_OPEN */

    ML_$EXCLUSION_STOP((void *)MSG_$SOCK_LOCK);
    *status_ret = status_$ok;

#else
    (void)socket;
    (void)depth;
    *status_ret = status_$msg_socket_out_of_range;
#endif
}

/*
 * MSG_$OPEN - Open socket wrapper
 *
 * Returns true (non-zero) on success, false (zero) on failure.
 */
void MSG_$OPEN(msg_$socket_t *socket, int16_t *depth, status_$t *status_ret)
{
    status_$t status;

    MSG_$OPENI(socket, depth, &status);
    *status_ret = status;
}
