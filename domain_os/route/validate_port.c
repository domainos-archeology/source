/*
 * ROUTE_$VALIDATE_PORT - Check network capability for node
 *
 * Checks if a network operation is supported for the given routing info.
 * Iterates through ROUTE_$PORTP[0..7] in ASCENDING order looking for a
 * matching port.
 *
 * Parameters:
 *   routing_key - Routing information (port network address)
 *   is_local    - Non-zero if querying local node
 *
 * Returns:
 *   0: Unknown network
 *   1: Network supports operation
 *   2: Operation not defined on hardware
 *
 * Original address: 0x00E65904
 * Original size: 118 bytes
 */

#include "route/route_internal.h"

int16_t ROUTE_$VALIDATE_PORT(int32_t routing_key, int8_t is_local)
{
    route_$port_t *port = NULL;      /* A6-0x0C, cleared at 0x00E65912 */
    int16_t i;

    /* 0x00E65916 tst.l D1 */
    if (routing_key == 0) {
        /*
         * 0x00E6591A-0x00E6592E: no key, so port 0 is the answer if it is
         * active at all.
         */
        if (ROUTE_$PORTP[0]->active == 0) {
            /*
             * 0x00E65926 `clr.w D0w`: only the LOW WORD of the result
             * register is cleared here, which is why this routine's result
             * is a word.
             */
            return 0;  /* Unknown network */
        }
        port = ROUTE_$PORTP[0];
    } else {
        /*
         * 0x00E65930-0x00E6594C: scan ROUTE_$PORTP[0..7] ASCENDING for the
         * first active port whose network matches.
         *
         *   00e65930  moveq #0x7,D2               ; dbf counter: 8 passes
         *   00e65932  movea.l #0xe26ee8,A0        ; &ROUTE_$PORTP[0]
         *   00e65938  movea.l (A0),A1             ; the port this slot names
         *   00e6593a  tst.w (0x2c,A1)             ; port->active
         *   00e65940  cmp.l (A1),D1               ; port->network
         *   00e6594a  addq.l #0x4,A0              ; forward one slot
         *   00e6594c  dbf D2w
         *
         * The cursor moves UP through the table, so with two active ports on
         * the same network the LOWER index wins.
         */
        for (i = 0; i <= 7; i++) {
            route_$port_t *p = ROUTE_$PORTP[i];

            if (p->active != 0 && (int32_t)p->network == routing_key) {
                port = p;
                break;
            }
        }
    }

    /* 0x00E65950-0x00E65954 tst.l (-0xc,A6) / bne */
    if (port == NULL) {
        /*
         * 0x00E65956-0x00E6595A: no matching port.  A Domain boolean TRUE
         * (high bit set) says the caller was asking about the local node, and
         * then the answer is "not defined on hardware".
         */
        if (is_local < 0) {
            return 2;
        }
        /* Fall through to return 1 */
    } else {
        /*
         * 0x00E6595C-0x00E6596A: bit 1 of the driver record's flags byte
         * (route_$driver_info_t + 0x07) is the "can route through this port"
         * capability.  driver_info is a target virtual address, so it is
         * reached with ARCH_VA_TO_PTR.
         *
         *   00e65960  movea.l (0x48,A1),A0
         *   00e65964  btst.b #0x1,(0x7,A0)
         *   00e6596a  beq.b                       ; clear -> 2
         */
        const route_$driver_info_t *driver =
            (const route_$driver_info_t *)ARCH_VA_TO_PTR(port->driver_info);

        if ((driver->flags & 0x02) == 0) {
            return 2;  /* Operation not defined on hardware */
        }
    }

    /* 0x00E6596C moveq #0x1,D0 */
    return 1;  /* Network supports operation */
}
