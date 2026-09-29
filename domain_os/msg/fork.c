/*
 * MSG_$FORK - Give a child address space the parent's socket ownership
 *
 * Walks every socket the parent owns and adds the child's bit to that
 * socket's ownership bitmap.
 *
 * Original address: 0x00E73F00 (142 bytes)
 */

#include "msg/msg_internal.h"

/*
 * @param parent_asid   Parent ASID, read as a WORD each time round the loop
 *                      ("movea.l D4,A2 / move.w (A2),D5w" at 0x00E73F2E)
 * @param child_asid    Child ASID, likewise (0x00E73F4C)
 *
 * @return a Domain boolean: true when at least one socket was shared
 *         (0x00E73F46 "st D2b", 0x00E73F82 "move.b D2b,D0b")
 */
boolean MSG_$FORK(uint16_t *parent_asid, uint16_t *child_asid)
{
    int16_t sock_num;
    int16_t parent;             /* D5w */
    int16_t child;              /* D5w */
    int16_t byte_index;         /* D1w */
    uint8_t *bitmap;            /* A3 / A2 */
    uint8_t child_ownership[8]; /* A6-0x14 */
    boolean shared_any = false; /* D2b, 0x00E73F10 "clr.b D2b" */
    int i;

    ML_$EXCLUSION_START(&MSG_$WIRED_DATA.sock_lock);        /* 0x00E73F12 */

    /*
     * 0x00E73F20 - 0x00E73F72
     *   movea.l #0xe80d84,A1 / move.w #0xdf,D0w / addq.l #0x8,A1
     *   ... lea (0x1d8,A0),A3 ... addq.l #0x8,A1 / dbf D0w
     * "moveq #0xdf" plus dbf is 0xE0 iterations, and A1 starts one slot in,
     * so the sockets visited are 1..0xE0 inclusive.
     */
    for (sock_num = 1; sock_num <= MSG_MAX_SOCKET; sock_num++) {
        bitmap = MSG_$UNWIRED_DATA.ownership[sock_num];

        /*
         * 0x00E73F30  moveq #0x3f,D1 / move.w (A2),D5w / sub.w D5w,D1w /
         *             lsr.w #0x3,D1w / btst.b D5,(0x0,A3,D1w*0x1)
         */
        parent = (int16_t)*parent_asid;
        byte_index = (int16_t)((uint16_t)(0x3F - parent) >> 3);

        if ((bitmap[byte_index] & (1 << (parent & 7))) != 0) {
            shared_any = true;                  /* 0x00E73F46  st D2b */

            /* 0x00E73F48  clr.l (A3)+ / clr.l (A3)+ */
            for (i = 0; i < 8; i++) {
                child_ownership[i] = 0;
            }

            /*
             * 0x00E73F4E  moveq #0x3f,D1 / move.w (A3),D5w / sub.w D5w,D1w /
             *             lsr.w #0x3,D1w / bset.b D5,(0x0,A4,D1w*0x1)
             */
            child = (int16_t)*child_asid;
            byte_index = (int16_t)((uint16_t)(0x3F - child) >> 3);
            child_ownership[byte_index] |= (uint8_t)(1 << (child & 7));

            /* 0x00E73F66  moveq #0x1,D5 / move.l (A4)+,D1 / or.l D1,(A2)+ / dbf */
            for (i = 0; i < 8; i++) {
                bitmap[i] |= child_ownership[i];
            }
        }
    }

    ML_$EXCLUSION_STOP(&MSG_$WIRED_DATA.sock_lock);         /* 0x00E73F76 */
    return shared_any;
}
