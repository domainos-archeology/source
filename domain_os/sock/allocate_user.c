/*
 * SOCK_$ALLOCATE_USER - Allocate a user-mode socket
 *
 * Allocates a socket for user-mode use. User-mode sockets are tracked
 * by a counter that limits the total number that can be allocated.
 * The socket is marked with the SOCK_FLAG_USER_MODE bit.
 *
 * Original address: 0x00E15F14
 * Original source: Pascal, converted to C
 */

#include "sock_internal.h"

int8_t SOCK_$ALLOCATE_USER(uint16_t *sock_ret, uint32_t proto_bufpages, uint32_t max_queue)
{
    uint16_t *user_limit;
    sock_ec_view_t *sock_view;
    int8_t result;
    uint16_t sock_num;

    /* Get pointer to user socket limit counter */
    user_limit = SOCK_GET_USER_LIMIT();

    /* Check if user sockets are available */
    if (*user_limit == 0) {
        /* No user sockets available */
        *sock_ret = 0;
        result = 0;
    } else {
        /* Try to allocate a socket from the free pool */
        result = SOCK_$ALLOCATE(sock_ret, proto_bufpages, max_queue);

        if (result < 0) {
            /* Successfully allocated - mark as user socket */

            /* Decrement user socket limit */
            (*user_limit)--;

            /* Get the allocated socket number */
            sock_num = *sock_ret;

            /* Get pointer to socket's EC view */
            sock_view = SOCK_GET_VIEW_PTR(sock_num);

            /* Set user-mode flag */
            sock_view->flags |= SOCK_FLAG_USER_MODE;

            result = -1;  /* 0xFF = success */
        } else {
            /* Allocation failed */
            *sock_ret = 0;
            result = 0;
        }
    }

    return result;
}
