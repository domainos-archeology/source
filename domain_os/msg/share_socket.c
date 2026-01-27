/*
 * MSG_$SHARE_SOCKET - Share socket with another address space
 *
 * Adds or removes ownership of a socket for another process.
 * The caller must own the socket.
 *
 * Original address: 0x00E5A02C
 * Original size: 250 bytes
 */

#include "msg/msg_internal.h"

/*
 * MSG_$SHARE_SOCKET - Share socket ownership
 *
 * Parameters:
 *   socket - Socket to share
 *   uid    - UID of target process
 *   add_remove - Non-zero to add, zero to remove
 *   status_ret - Status return
 */
void MSG_$SHARE_SOCKET(msg_$socket_t *socket, uid_t *uid, int16_t *add_remove,
                        status_$t *status_ret)
{
#if defined(M68K)
    int16_t sock_num;
    int16_t sock_offset;
    uint8_t asid;
    uint8_t target_asid;
    uint8_t byte_index;
    uint8_t *bitmap;
    uint8_t target_ownership[8];
    uint8_t new_ownership[8];
    int i;

    /* Lock the socket table */
    ML_$EXCLUSION_START((void *)MSG_$SOCK_LOCK);

    sock_num = *socket;

    /* Validate socket number */
    if (sock_num < 1 || sock_num > MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;
        ML_$EXCLUSION_STOP((void *)MSG_$SOCK_LOCK);
        return;
    }

    /* Check current process owns the socket */
    asid = PROC1_$AS_ID;
    sock_offset = sock_num << 3;
    bitmap = (uint8_t *)(MSG_$DATA_BASE + MSG_OFF_OWNERSHIP + sock_offset);
    byte_index = (0x3F - asid) >> 3;

    if ((bitmap[byte_index] & (1 << (asid & 7))) == 0) {
        *status_ret = status_$msg_no_owner;
        ML_$EXCLUSION_STOP((void *)MSG_$SOCK_LOCK);
        return;
    }

    /* Get ASID for target UID */
    target_asid = (uint8_t)PROC2_$FIND_ASID(uid, NULL, status_ret);
    if (*status_ret != status_$ok) {
        ML_$EXCLUSION_STOP((void *)MSG_$SOCK_LOCK);
        return;
    }

    /*
     * Build ownership bitmap for target ASID.
     */
    for (i = 0; i < 8; i++) {
        target_ownership[i] = 0;
    }
    byte_index = (0x3F - target_asid) >> 3;
    target_ownership[byte_index] |= (1 << (target_asid & 7));

    if (*add_remove == 0) {
        /*
         * Remove ownership: new = old & ~target
         */
        for (i = 0; i < 8; i++) {
            new_ownership[i] = bitmap[i] & ~target_ownership[i];
        }
    } else {
        /*
         * Add ownership: new = old | target
         */
        for (i = 0; i < 8; i++) {
            new_ownership[i] = bitmap[i] | target_ownership[i];
        }
    }

    /* Store new ownership */
    for (i = 0; i < 8; i++) {
        bitmap[i] = new_ownership[i];
    }

    *status_ret = status_$ok;
    ML_$EXCLUSION_STOP((void *)MSG_$SOCK_LOCK);

#else
    (void)socket;
    (void)uid;
    (void)add_remove;
    *status_ret = status_$msg_socket_out_of_range;
#endif
}
