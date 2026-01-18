/*
 * MSG_$ALLOCATE, MSG_$ALLOCATEI - Allocate a specific socket
 *
 * Allocates a specific socket number, finding a free socket if needed.
 * Used when the caller needs to specify which socket number to use.
 *
 * Original addresses:
 *   MSG_$ALLOCATE:  0x00E592CA (28 bytes)
 *   MSG_$ALLOCATEI: 0x00E592E6 (236 bytes)
 */

#include "msg/msg_internal.h"

/*
 * Network service callback
 */
extern void MSG_$NET_SERVICE(void);

/*
 * MSG_$ALLOCATEI - Allocate socket internal implementation
 *
 * Parameters:
 *   socket - Pointer to socket number (updated with allocated socket)
 *   depth  - Pointer to socket depth
 *   status_ret - Status return
 */
void MSG_$ALLOCATEI(msg_$socket_t *socket, int16_t *depth, status_$t *status_ret)
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

    sock_depth = *depth;

    /* Validate depth (max 32) */
    if (sock_depth > MSG_MAX_DEPTH) {
        *status_ret = status_$msg_too_deep;
        return;
    }

    /* Lock the socket table */
    ML_$EXCLUSION_START((void *)MSG_$SOCK_LOCK);

    /*
     * Try to allocate a socket using the lower-level allocator.
     * SOCK_$ALLOCATE_USER finds a free socket and returns it in *socket.
     * Returns negative on success.
     */
    if (SOCK_$ALLOCATE_USER(socket, sock_depth, sock_depth, sock_depth, 0x0400) >= 0) {
        ML_$EXCLUSION_STOP((void *)MSG_$SOCK_LOCK);
        *status_ret = status_$msg_no_more_sockets;
        return;
    }

    sock_num = *socket;

    /*
     * Build ownership bitmap with current ASID's bit set.
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

    /* Calculate offset into ownership table */
    sock_offset = sock_num << 3;
    bitmap = (uint8_t *)(MSG_$DATA_BASE + MSG_OFF_OWNERSHIP + sock_offset);

    /* Store ownership bitmap */
    *(uint32_t *)bitmap = *(uint32_t *)ownership;
    *(uint32_t *)(bitmap + 4) = *(uint32_t *)(ownership + 4);

    /* Store socket depth */
    *(int16_t *)(MSG_$DATA_BASE + MSG_OFF_DEPTH_TABLE + sock_num * 2) = sock_depth;

    /* Increment open socket count */
    (*(int16_t *)(MSG_$DATA_BASE + MSG_OFF_OPEN_COUNT))++;

    /* Register process cleanup handler */
    PROC2_$SET_CLEANUP(7);

    /* Register network service */
    service_type = 0x80000;
    NETWORK_$SET_SERVICE(MSG_$NET_SERVICE, &service_type, &net_status);

    /* Mark that user sockets are open */
    *(uint8_t *)0xE24C48 = 0xFF;  /* NETWORK_$USER_SOCK_OPEN */

    ML_$EXCLUSION_STOP((void *)MSG_$SOCK_LOCK);
    *status_ret = status_$ok;

#else
    (void)socket;
    (void)depth;
    *status_ret = status_$msg_too_deep;
#endif
}

/*
 * MSG_$ALLOCATE - Allocate socket wrapper
 *
 * Returns true (non-zero) on success, false (zero) on failure.
 */
int8_t MSG_$ALLOCATE(msg_$socket_t *socket, int16_t *depth, status_$t *status_ret)
{
    status_$t status;

    MSG_$ALLOCATEI(socket, depth, &status);
    *status_ret = status;
    return (status == status_$ok) ? -1 : 0;
}
