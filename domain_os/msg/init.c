/*
 * MSG_$INIT - Initialize the MSG subsystem
 *
 * Claims the single network bounce page, maps it, initialises the socket
 * exclusion lock and pre-assigns the five reserved sockets.
 *
 * Original address: 0x00E31B84 (144 bytes)
 */

#include "msg/msg_internal.h"

/*
 * The ownership word MSG_$INIT stores in five bitmaps (0x00E31BD4 onwards).
 * As bytes it is 04 00 00 00: byte index 0 with bit 2 set, i.e. the ASID for
 * which (0x3F - asid) >> 3 == 0 and asid & 7 == 2 - ASID 0x3A.
 */
#define MSG_INIT_OWNER_BYTE0    0x04

/*
 * The sockets the initialiser hands to that ASID.  The stores are at
 * &MSG_$UNWIRED_DATA + 0x1E0, 0x1E8, 0x1F8, 0x200 and 0x208, and the ownership
 * table starts at +0x1D8 with a stride of 8, so these are sockets 1, 2, 4, 5
 * and 6.  Socket 3 (+0x1F0) is deliberately skipped.
 */
static const int16_t MSG_$INIT_SOCKETS[5] = { 1, 2, 4, 5, 6 };

void MSG_$INIT(void)
{
    status_$t status;
    int i;
    int j;

    /* 0x00E31B88  move.l #0xe242fc,-(SP) - the data page's physical address */
    NETBUF_$GET_DAT(&MSG_$WIRED_DATA.dpage_pa);

    /* 0x00E31B96 - 0x00E31BAC */
    NETBUF_$GETVA(MSG_$WIRED_DATA.dpage_pa, &MSG_$WIRED_DATA.dpage_va, &status);

    /* 0x00E31BB0  tst.l (-0x4,A6) / beq */
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);                  /* 0x00E31BBA */
    }

    ML_$EXCLUSION_INIT(&MSG_$WIRED_DATA.sock_lock);         /* 0x00E31BC2 */

    /*
     * 0x00E31BCE - 0x00E31C10: five pairs of "move.l #0x4000000,(off,A0)"
     * and "clr.l (off+4,A0)", i.e. each bitmap set to 04 00 00 00 00 00 00 00.
     */
    for (i = 0; i < 5; i++) {
        uint8_t *bitmap = MSG_$UNWIRED_DATA.ownership[MSG_$INIT_SOCKETS[i]];

        bitmap[0] = MSG_INIT_OWNER_BYTE0;
        for (j = 1; j < 8; j++) {
            bitmap[j] = 0;
        }
    }
}
