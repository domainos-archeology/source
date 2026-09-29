/*
 * MSG_$CLOSE, MSG_$CLOSEI - Close a message socket
 *
 * MSG_$CLOSEI drops the calling address space's ownership bit from the
 * socket's 8-byte ownership mask; when the mask goes to zero the underlying
 * socket is SOCK_$CLOSEd, the open count is decremented and, if it reaches
 * zero, user sockets are flagged closed and MSG's network service removed.
 * MSG_$CLOSE is the one-argument SVC form (SVC_$TRAP2_TABLE[0x0F]) that
 * throws the status away.
 *
 * Original addresses:
 *   MSG_$CLOSE:  0x00E593D2 (18 bytes)
 *   MSG_$CLOSEI: 0x00E593E4 (270 bytes)
 * Both in the map's MSG_UNWIRED object at 0xE5911C (size 0x100C).
 * Re-emitted from the disassembly 0x00E593D2-0x00E594F0.
 */

#include "msg/msg_internal.h"

/*
 * MSG_$CLOSEI - Close socket, internal form
 *
 * Parameters:
 *   socket     - pointer to the socket number word (argument 1, (0x8,A6))
 *   status_ret - Status return (argument 2, (0xC,A6))
 */
void MSG_$CLOSEI(msg_$socket_t *socket, status_$t *status_ret)
{
    uint8_t   ownership[8];     /* (-0x10,A6): this AS's mask, then new mask */
    uint32_t  service_flags;    /* (-0x8,A6)                                 */
    status_$t net_status;       /* (-0x4,A6)                                 */
    uint16_t  asid;             /* D1w                                       */
    uint16_t  byte_index;       /* D0w                                       */
    uint8_t  *bitmap;           /* A0 + 0x1D8                                */
    int       k;

    /* 0x00E593FA-0x00E5940C: socket must be 1..0xE0 (signed word tests:
     * `ble` on the value, `cmpi.w #0xe0 / ble` accepts 0xE0). */
    if ((int16_t)*socket <= 0 || (int16_t)*socket > MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;         /* 0x290001 */
        return;
    }

    /* 0x00E59410-0x00E5941C: ML_$EXCLUSION_START(0xE242E4). */
    ML_$EXCLUSION_START(&MSG_$WIRED_DATA.sock_lock);

    /* 0x00E5941E-0x00E5943A: bitmap = base + socket*8 + 0x1D8; test bit
     * (asid & 7) of byte (0x3F - asid) >> 3. */
    bitmap     = MSG_$UNWIRED_DATA.ownership[*socket];
    asid       = PROC1_$AS_ID;
    byte_index = (uint16_t)((uint16_t)(0x3F - asid) >> 3);
    if ((bitmap[byte_index] & (uint8_t)(1 << (asid & 7))) == 0) {
        /* 0x00E5943C-0x00E59450: not an owner - status, unlock, out. */
        *status_ret = status_$msg_no_owner;                    /* 0x290005 */
        ML_$EXCLUSION_STOP(&MSG_$WIRED_DATA.sock_lock);
        return;
    }

    /* 0x00E59454-0x00E59466: build this AS's mask. */
    ownership[0] = 0; ownership[1] = 0; ownership[2] = 0; ownership[3] = 0;
    ownership[4] = 0; ownership[5] = 0; ownership[6] = 0; ownership[7] = 0;
    ownership[byte_index] |= (uint8_t)(1 << (asid & 7));

    /* 0x00E5946A-0x00E59480: two longwords, `dbf #1`: mask = ~mask & old,
     * written back over the local mask. */
    for (k = 0; k < 8; k++) {
        ownership[k] = (uint8_t)(~ownership[k] & bitmap[k]);
    }

    /* 0x00E59484-0x00E5948C: store the new mask. */
    for (k = 0; k < 8; k++) {
        bitmap[k] = ownership[k];
    }

    /* 0x00E59490-0x00E5949C: any owner left?  (`tst.l (A1)+` twice) */
    if ((bitmap[0] | bitmap[1] | bitmap[2] | bitmap[3]) == 0
        && (bitmap[4] | bitmap[5] | bitmap[6] | bitmap[7]) == 0) {
        /* 0x00E5949E: open_count-- */
        MSG_$UNWIRED_DATA.open_count--;

        /* 0x00E594A2-0x00E594AC: SOCK_$CLOSE(*socket) (word, with a
         * spare result slot). */
        SOCK_$CLOSE(*socket);

        /* 0x00E594AE-0x00E594B2: last one out? */
        if (MSG_$UNWIRED_DATA.open_count == 0) {
            /* 0x00E594B4: clr.b NETWORK_$USER_SOCK_OPEN */
            NETWORK_$USER_SOCK_OPEN = 0;

            /* 0x00E594BA-0x00E594D4: NETWORK_$SET_SERVICE(
             * &MSG_$NET_SERVICE_CLOSE, &0x80000, &net_status); the op word
             * is the PC-relative cell at 0x00E594F2 (`pea (0x26,PC)`). */
            service_flags = 0x80000;
            NETWORK_$SET_SERVICE((int16_t *)&MSG_$NET_SERVICE_CLOSE,
                                 &service_flags, &net_status);
        }
    }

    /* 0x00E594D8-0x00E594E6: ML_$EXCLUSION_STOP(0xE242E4); status ok. */
    ML_$EXCLUSION_STOP(&MSG_$WIRED_DATA.sock_lock);
    *status_ret = status_$ok;

    /* 0x00E594E8-0x00E594F0 */
}

/*
 * MSG_$CLOSE - Close socket, SVC form
 *
 * One argument only: the status goes to a local (`pea (-0x4,A6)`,
 * 0x00E593D6) that is never read - the caller learns nothing.
 *
 * Parameters:
 *   socket - pointer to the socket number word (argument 1, (0x8,A6))
 */
void MSG_$CLOSE(msg_$socket_t *socket)
{
    status_$t status;   /* (-0x4,A6) */

    /* 0x00E593D6-0x00E593DE */
    MSG_$CLOSEI(socket, &status);
}
