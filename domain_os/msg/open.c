/*
 * MSG_$OPEN, MSG_$OPENI - Open a message socket by number
 *
 * MSG_$OPENI opens the socket the caller names (1..0xDF) with SOCK_$OPEN
 * sized by *depth, provided nobody owns it yet, then records the calling
 * address space as its owner, stores the depth, bumps the open count,
 * registers the process cleanup bit, adds MSG's network service and flags
 * user sockets open.  MSG_$OPEN is the two-argument SVC form
 * (SVC_$TRAP2_TABLE[0x14]) that returns a Domain boolean instead of a
 * status.
 *
 * Original addresses:
 *   MSG_$OPEN:  0x00E59198 (28 bytes)
 *   MSG_$OPENI: 0x00E591B4 (276 bytes)
 * Both in the map's MSG_UNWIRED object at 0xE5911C (size 0x100C).
 * Re-emitted from the disassembly 0x00E59198-0x00E592C6.
 */

#include "msg/msg_internal.h"

/*
 * MSG_$OPENI - Open socket, internal form
 *
 * Parameters:
 *   socket     - pointer to the socket number word (argument 1, (0x8,A6))
 *   depth      - pointer to the socket depth word (argument 2, (0xC,A6))
 *   status_ret - Status return (argument 3, (0x10,A6))
 */
void MSG_$OPENI(msg_$socket_t *socket, int16_t *depth, status_$t *status_ret)
{
    uint8_t   ownership[8];     /* (-0x14,A6): this AS's ownership mask */
    status_$t net_status;       /* (-0x8,A6)                             */
    uint32_t  service_flags;    /* (-0x4,A6)                             */
    uint16_t  asid;             /* D1w                                   */
    uint16_t  byte_index;       /* D0w                                   */
    uint8_t  *bitmap;           /* D2 + 0x1D8                            */

    /* 0x00E591CE-0x00E591DE: socket must be 1..0xDF (signed word tests:
     * `ble` on the value, `cmpi.w #0xe0 / blt` rejects 0xE0 itself). */
    if ((int16_t)*socket <= 0 || (int16_t)*socket >= MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;         /* 0x290001 */
        return;
    }

    /* 0x00E591E2-0x00E591EE: depth above 32 is status_$msg_too_deep. */
    if (*depth > MSG_MAX_DEPTH) {
        *status_ret = status_$msg_too_deep;                    /* 0x290002 */
        return;
    }

    /* 0x00E591F2-0x00E591FE: ML_$EXCLUSION_START(0xE242E4). */
    ML_$EXCLUSION_START(&MSG_$WIRED_DATA.sock_lock);

    /* 0x00E59200-0x00E59216: bitmap = base + socket*8 + 0x1D8; both
     * longwords must be zero (`tst.l (A1)+` twice), else in use. */
    bitmap = MSG_$UNWIRED_DATA.ownership[*socket];
    if ((bitmap[0] | bitmap[1] | bitmap[2] | bitmap[3]) != 0
        || (bitmap[4] | bitmap[5] | bitmap[6] | bitmap[7]) != 0) {
        /* 0x00E59234-0x00E59246 */
        *status_ret = status_$msg_socket_in_use;               /* 0x290008 */
        ML_$EXCLUSION_STOP(&MSG_$WIRED_DATA.sock_lock);
        return;
    }

    /* 0x00E59218-0x00E59232: SOCK_$OPEN(*socket, depth:depth, depth:0x400)
     * - five words pushed: 0x400, (A2), (A2), (A2), (A3), so the two
     * longword arguments are (depth << 16 | depth) and (depth << 16 | 0x400).
     * A non-negative (false) result is treated as "in use" too. */
    if (SOCK_$OPEN((uint16_t)*socket,
                   ((uint32_t)(uint16_t)*depth << 16) | (uint16_t)*depth,
                   ((uint32_t)(uint16_t)*depth << 16) | 0x0400) >= 0) {
        /* 0x00E59234-0x00E59246 */
        *status_ret = status_$msg_socket_in_use;               /* 0x290008 */
        ML_$EXCLUSION_STOP(&MSG_$WIRED_DATA.sock_lock);
        return;
    }

    /* 0x00E59248-0x00E59260: zero the 8-byte mask, then set bit (asid & 7)
     * of byte (0x3F - asid) >> 3. */
    ownership[0] = 0; ownership[1] = 0; ownership[2] = 0; ownership[3] = 0;
    ownership[4] = 0; ownership[5] = 0; ownership[6] = 0; ownership[7] = 0;
    asid       = PROC1_$AS_ID;
    byte_index = (uint16_t)((uint16_t)(0x3F - asid) >> 3);
    ownership[byte_index] |= (uint8_t)(1 << (asid & 7));

    /* 0x00E59264-0x00E5926E: store the two longwords. */
    bitmap[0] = ownership[0]; bitmap[1] = ownership[1];
    bitmap[2] = ownership[2]; bitmap[3] = ownership[3];
    bitmap[4] = ownership[4]; bitmap[5] = ownership[5];
    bitmap[6] = ownership[6]; bitmap[7] = ownership[7];

    /* 0x00E59272-0x00E59276: depth[*socket] = *depth (word, stride 2). */
    MSG_$UNWIRED_DATA.depth[*socket] = *depth;

    /* 0x00E5927A: open_count++ */
    MSG_$UNWIRED_DATA.open_count++;

    /* 0x00E5927E-0x00E5928A: PROC2_$SET_CLEANUP(7). */
    PROC2_$SET_CLEANUP(7);

    /* 0x00E5928C-0x00E592A6: NETWORK_$SET_SERVICE(&MSG_$NET_SERVICE,
     * &0x80000, &net_status); the op word is the PC-relative cell at
     * 0x00E592C8 (`pea (0x2a,PC)`). */
    service_flags = 0x80000;
    NETWORK_$SET_SERVICE((int16_t *)&MSG_$NET_SERVICE, &service_flags, &net_status);

    /* 0x00E592AA: st NETWORK_$USER_SOCK_OPEN */
    NETWORK_$USER_SOCK_OPEN = (int8_t)0xFF;

    /* 0x00E592B0-0x00E592BC: ML_$EXCLUSION_STOP(0xE242E4); status ok. */
    ML_$EXCLUSION_STOP(&MSG_$WIRED_DATA.sock_lock);
    *status_ret = status_$ok;

    /* 0x00E592BE-0x00E592C6 */
}

/*
 * MSG_$OPEN - Open socket, SVC form
 *
 * Two arguments only: the status goes to a local the caller never sees
 * (`pea (-0x4,A6)`, 0x00E5919C) and the result is `seq D0b` on it -
 * 0xFF (true) when the open succeeded, 0 otherwise.
 *
 * Parameters:
 *   socket - pointer to the socket number word (argument 1, (0x8,A6))
 *   depth  - pointer to the socket depth word (argument 2, (0xC,A6))
 */
boolean MSG_$OPEN(msg_$socket_t *socket, int16_t *depth)
{
    status_$t status;   /* (-0x4,A6) */

    /* 0x00E5919C-0x00E591A8 */
    MSG_$OPENI(socket, depth, &status);

    /* 0x00E591AA-0x00E591AE: seq D0b */
    return (status == status_$ok) ? (boolean)0xFF : (boolean)0;
}
