/*
 * MSG_$SHARE_SOCKET - add or remove another address space's ownership of a
 * socket.
 *
 * The caller's own address space must already own the socket.  The target is
 * named by process UID; PROC2_$GET_ASID turns it into an ASID and that ASID's
 * bit is then set in (or cleared from) the socket's 8-byte ownership bitmap.
 *
 * Original address: 0x00E5A02C, size 250 bytes (0x00E5A02C-0x00E5A125).
 * A5 = 0x00E80D84 (the MSG_ module data base).
 */

#include "msg/msg_internal.h"

/*
 * MSG_$SHARE_SOCKET
 *
 * Parameters (0x08, 0x0C, 0x10, 0x14 off A6):
 *   socket      - pointer to the socket number (1..MSG_MAX_SOCKET)
 *   uid         - UID of the process to share with / stop sharing with
 *   add_remove  - word: 0 removes the target's ownership, anything else adds it
 *                 ("movea.l (0x10,A6),A0 / tst.w (A0) / bne" at 0x00E5A09C)
 *   status_ret  - status return
 *
 * The image writes status_ret only on the two error paths and through
 * PROC2_$GET_ASID; there is no status_$ok store on the success path, so this
 * routine leaves the caller's status word alone when it succeeds.
 */
void MSG_$SHARE_SOCKET(msg_$socket_t *socket, uid_t *uid, int16_t *add_remove,
                       status_$t *status_ret)
{
    int16_t     sock_num;
    uint16_t    my_asid;
    uint16_t    target_asid;
    uint16_t    byte_index;
    uint8_t    *owners;
    uint8_t     target_bits[8];
    uint8_t     new_owners[8];
    int         i;

    /* 0x00E5A042: ML_$EXCLUSION_START(&MSG_$WIRED_DATA.sock_lock) */
    ML_$EXCLUSION_START(&MSG_$WIRED_DATA.sock_lock);

    /* 0x00E5A050-0x00E5A062: 1 <= socket <= 0xE0 */
    sock_num = *socket;
    if (sock_num < 1 || sock_num > MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;
        goto done;
    }

    owners = MSG_$UNWIRED_DATA.ownership[sock_num];

    /*
     * 0x00E5A06A-0x00E5A07E: the caller's address space must already own the
     * socket.
     *   moveq  #0x3f,D0
     *   move.w (PROC1_$AS_ID),D1w
     *   sub.w  D1w,D0w / lsr.w #0x3,D0w
     *   btst.b D1,(0x0,A0,D0w*0x1)
     * The byte index is computed in word arithmetic with a logical shift and
     * the bit number is taken modulo 8 by btst.
     */
    my_asid = (uint16_t)PROC1_$AS_ID;
    byte_index = (uint16_t)(0x3F - my_asid) >> 3;
    if ((owners[byte_index] & (1u << (my_asid & 7))) == 0) {
        *status_ret = status_$msg_no_owner;
        goto done;
    }

    /*
     * 0x00E5A08A-0x00E5A096: PROC2_$GET_ASID(uid, status_ret), two arguments
     * ("pea (A4) / move.l (0xc,A6),-(SP) / jsr 0x00e40702.l / addq.w #0x8,SP").
     * The ASID comes back in D0.
     */
    target_asid = PROC2_$GET_ASID(uid, status_ret);
    if (*status_ret != status_$ok) {
        goto done;
    }

    /*
     * 0x00E5A0A4-0x00E5A0B8 (and the identical block at 0x00E5A0D6): an empty
     * 8-byte set with just the target ASID's bit in it.
     */
    for (i = 0; i < 8; i++) {
        target_bits[i] = 0;
    }
    byte_index = (uint16_t)(0x3F - target_asid) >> 3;
    target_bits[byte_index] |= (uint8_t)(1u << (target_asid & 7));

    if (*add_remove == 0) {
        /*
         * 0x00E5A0BA-0x00E5A0D0: two longwords of new = ~target & owners
         *   move.l (A3)+,D1 / not.l D1 / and.l (A1)+,D1 / move.l D1,(A4)+
         */
        for (i = 0; i < 8; i++) {
            new_owners[i] = (uint8_t)(~target_bits[i] & owners[i]);
        }
    } else {
        /*
         * 0x00E5A0EC-0x00E5A100: two longwords of new = owners | target
         *   move.l (A3)+,D1 / or.l (A1)+,D1 / move.l D1,(A4)+
         */
        for (i = 0; i < 8; i++) {
            new_owners[i] = (uint8_t)(owners[i] | target_bits[i]);
        }
    }

    /* 0x00E5A104-0x00E5A10C: store both longwords back */
    for (i = 0; i < 8; i++) {
        owners[i] = new_owners[i];
    }

done:
    /* 0x00E5A110: ML_$EXCLUSION_STOP(&MSG_$WIRED_DATA.sock_lock) */
    ML_$EXCLUSION_STOP(&MSG_$WIRED_DATA.sock_lock);
}
