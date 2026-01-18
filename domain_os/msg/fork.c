/*
 * MSG_$FORK - Duplicate socket ownership for fork
 *
 * Copies all socket ownership from parent ASID to child ASID.
 * Called during process fork to share all open sockets with the child.
 *
 * Original address: 0x00E73F00
 * Original size: 142 bytes
 */

#include "msg/msg_internal.h"

/*
 * MSG_$FORK - Copy socket ownership from parent to child
 *
 * Parameters:
 *   parent_asid - Pointer to parent's ASID
 *   child_asid  - Pointer to child's ASID
 *
 * Returns:
 *   Non-zero if any sockets were shared, zero otherwise
 */
int8_t MSG_$FORK(uint16_t *parent_asid, uint16_t *child_asid)
{
#if defined(M68K)
    int16_t sock_num;
    uint8_t parent;
    uint8_t child;
    uint8_t parent_byte_index;
    uint8_t child_byte_index;
    uint8_t *bitmap;
    uint8_t child_ownership[8];
    int8_t shared_any = 0;
    int i;

    parent = (uint8_t)*parent_asid;
    child = (uint8_t)*child_asid;

    /* Lock the socket table */
    ML_$EXCLUSION_START((void *)MSG_$SOCK_LOCK);

    /*
     * Iterate through all sockets (1-223).
     * For each socket owned by parent, add child as owner.
     */
    for (sock_num = 1; sock_num < MSG_MAX_SOCKET; sock_num++) {
        bitmap = (uint8_t *)(MSG_$DATA_BASE + MSG_OFF_OWNERSHIP + sock_num * 8);

        /* Check if parent owns this socket */
        parent_byte_index = (0x3F - parent) >> 3;
        if ((bitmap[parent_byte_index] & (1 << (parent & 7))) != 0) {
            /* Parent owns this socket - add child ownership */
            shared_any = -1;  /* 0xFF = true */

            /* Build child ownership bitmap */
            for (i = 0; i < 8; i++) {
                child_ownership[i] = 0;
            }
            child_byte_index = (0x3F - child) >> 3;
            child_ownership[child_byte_index] |= (1 << (child & 7));

            /* Add child ownership: bitmap |= child_ownership */
            for (i = 0; i < 8; i++) {
                bitmap[i] |= child_ownership[i];
            }
        }
    }

    ML_$EXCLUSION_STOP((void *)MSG_$SOCK_LOCK);
    return shared_any;

#else
    (void)parent_asid;
    (void)child_asid;
    return 0;
#endif
}
