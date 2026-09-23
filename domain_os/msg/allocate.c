/*
 * MSG_$ALLOCATE, MSG_$ALLOCATEI - Allocate a message socket
 *
 * MSG_$ALLOCATEI asks SOCK_$ALLOCATE_USER for a free user socket sized by
 * *depth, then records the calling address space as the socket's owner,
 * stores the depth, bumps the open count, registers the process cleanup
 * bit, adds MSG's network service and flags user sockets open.
 * MSG_$ALLOCATE is the two-argument SVC form (SVC_$TRAP2_TABLE[0x13]) that
 * returns a Domain boolean instead of a status.
 *
 * Original addresses:
 *   MSG_$ALLOCATE:  0x00E592CA (28 bytes)
 *   MSG_$ALLOCATEI: 0x00E592E6 (236 bytes)
 * Both in the map's MSG_UNWIRED object at 0xE5911C (size 0x100C).
 * Re-emitted from the disassembly 0x00E592CA-0x00E593D0.
 */

#include "msg/msg_internal.h"

/*
 * MSG_$ALLOCATEI - Allocate socket, internal form
 *
 * Parameters:
 *   socket     - Output: allocated socket number (argument 1, (0x8,A6))
 *   depth      - pointer to the socket depth word (argument 2, (0xC,A6))
 *   status_ret - Status return (argument 3, (0x10,A6))
 */
void MSG_$ALLOCATEI(msg_$socket_t *socket, int16_t *depth, status_$t *status_ret)
{
    uint8_t   ownership[8];     /* (-0x10,A6): this AS's ownership mask */
    uint32_t  service_flags;    /* (-0x8,A6)                             */
    status_$t net_status;       /* (-0x4,A6)                             */
    uint16_t  asid;             /* D1w                                   */
    uint16_t  byte_index;       /* D0w                                   */
    uint8_t  *bitmap;           /* A0 + 0x1D8                            */

    /* 0x00E59300-0x00E5930C: `cmpi.w #0x20,(A3) / ble` - depth above 32
     * is status_$msg_too_deep. */
    if (*depth > MSG_MAX_DEPTH) {
        *status_ret = status_$msg_too_deep;                    /* 0x290002 */
        return;
    }

    /* 0x00E59310-0x00E5931C: ML_$EXCLUSION_START(0xE242E4). */
    ML_$EXCLUSION_START(MSG_$SOCK_LOCK);

    /* 0x00E5931E-0x00E59336: SOCK_$ALLOCATE_USER(socket, depth, depth,
     * depth, 0x400) - five words pushed: 0x400, (A3), (A3), (A3), then the
     * socket pointer.  A non-negative (false) result means no socket. */
    if (SOCK_$ALLOCATE_USER(socket, (uint16_t)*depth, (uint16_t)*depth,
                            (uint16_t)*depth, 0x0400) >= 0) {
        /* 0x00E593B6-0x00E593C2 */
        ML_$EXCLUSION_STOP(MSG_$SOCK_LOCK);
        *status_ret = status_$msg_no_more_sockets;              /* 0x290004 */
        return;
    }

    /* 0x00E59338-0x00E59350: zero the 8-byte mask, then set bit (asid & 7)
     * of byte (0x3F - asid) >> 3 - `bset.b D1,(0x0,A0,D0w*0x1)` numbers
     * bits modulo 8. */
    ownership[0] = 0; ownership[1] = 0; ownership[2] = 0; ownership[3] = 0;
    ownership[4] = 0; ownership[5] = 0; ownership[6] = 0; ownership[7] = 0;
    asid       = PROC1_$AS_ID;
    byte_index = (uint16_t)((uint16_t)(0x3F - asid) >> 3);
    ownership[byte_index] |= (uint8_t)(1 << (asid & 7));

    /* 0x00E59354-0x00E59364: copy the two longwords into
     * MSG_$SOCK_OWNERS[*socket] (base + socket*8 + 0x1D8). */
    bitmap = MSG_$SOCK_OWNERS[*socket];
    bitmap[0] = ownership[0]; bitmap[1] = ownership[1];
    bitmap[2] = ownership[2]; bitmap[3] = ownership[3];
    bitmap[4] = ownership[4]; bitmap[5] = ownership[5];
    bitmap[6] = ownership[6]; bitmap[7] = ownership[7];

    /* 0x00E59368-0x00E5936C: depth[*socket] = *depth (word, stride 2). */
    MSG_$DATA->depth[*socket] = *depth;

    /* 0x00E59370: open_count++ */
    MSG_$DATA->open_count++;

    /* 0x00E59374-0x00E59380: PROC2_$SET_CLEANUP(7) (with a spare result
     * slot the compiler reserved). */
    PROC2_$SET_CLEANUP(7);

    /* 0x00E59382-0x00E5939C: NETWORK_$SET_SERVICE(&MSG_$NET_SERVICE,
     * &0x80000, &net_status); the op word is the PC-relative cell at
     * 0x00E592C8 (`pea (-0xcc,PC)`). */
    service_flags = 0x80000;
    NETWORK_$SET_SERVICE((int16_t *)&MSG_$NET_SERVICE, &service_flags, &net_status);

    /* 0x00E593A0: st NETWORK_$USER_SOCK_OPEN */
    NETWORK_$USER_SOCK_OPEN = (int8_t)0xFF;

    /* 0x00E593A6-0x00E593B2: ML_$EXCLUSION_STOP(0xE242E4); status ok. */
    ML_$EXCLUSION_STOP(MSG_$SOCK_LOCK);
    *status_ret = status_$ok;

    /* 0x00E593C8-0x00E593D0 */
}

/*
 * MSG_$ALLOCATE - Allocate socket, SVC form
 *
 * Two arguments only: the status goes to a local the caller never sees
 * (`pea (-0x4,A6)`, 0x00E592CE) and the result is `seq D0b` on it -
 * 0xFF (true) when the allocation succeeded, 0 otherwise.
 *
 * Parameters:
 *   socket - Output: allocated socket number (argument 1, (0x8,A6))
 *   depth  - pointer to the socket depth word (argument 2, (0xC,A6))
 */
boolean MSG_$ALLOCATE(msg_$socket_t *socket, int16_t *depth)
{
    status_$t status;   /* (-0x4,A6) */

    /* 0x00E592CE-0x00E592DA */
    MSG_$ALLOCATEI(socket, depth, &status);

    /* 0x00E592DC-0x00E592E0: seq D0b */
    return (status == status_$ok) ? (boolean)0xFF : (boolean)0;
}
