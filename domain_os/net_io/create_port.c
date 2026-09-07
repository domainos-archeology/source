/*
 * NET_IO_$CREATE_PORT - Create a network I/O port
 *
 * Chooses a slot in ROUTE_$PORTP, fills in the port record from the caller's
 * driver block, and, for a user routing port, allocates the socket and the
 * ROUTE_$USER_STAT record that goes with it.
 *
 * Callers:
 *   RING_$INIT      0x00E2FB8E  (port type 0, the ring driver at
 *                                RING_$CTL + 0x518)
 *   ROUTE_$SERVICE  0x00E6A182  (port type 1 -> NET_IO_$NIL_DRIVER,
 *                                anything else -> NET_IO_$USER_DRIVER)
 *
 * Stack frame (link.w A6,-0x14): port_type word at 8(A6), unit word at
 * 0xA(A6), driver long at 0xC(A6), queue_length word at 0x10(A6), status_ret
 * long at 0x12(A6).  The result is the word local at A6-0x8, returned in
 * D0.w.
 *
 * Original address: 0x00E5A4A4
 * Original size: 538 bytes (0x00E5A4A4 .. 0x00E5A6B9)
 */

#include "net_io/net_io_internal.h"

int16_t NET_IO_$CREATE_PORT(int16_t port_type, uint16_t unit,
                            void *driver, uint16_t queue_length,
                            status_$t *status_ret)
{
    int16_t         result;         /* A6-0x8 */
    int16_t         port_index;     /* D4w */
    int32_t         port_index_l;   /* D5 */
    route_$port_t  *port;           /* A2 / A3 */
    uint8_t        *port_bytes;
    int16_t         stat_index;     /* D2w, reused after the record scan */
    int             i;

    /* 0x00E5A4C2  move.w #-0x1,(-0x8,A6) */
    result = -1;

    /*
     * 0x00E5A4C8  moveq #0x6,D0 / btst.l D2,D0
     * Bits 1 and 2 of the constant 6: port types 1 and 2 are the software
     * ports ROUTE_$SERVICE builds and may be created more than once, so only
     * the other types are checked for a duplicate.
     */
    if (((1 << (port_type & 0x1F)) & 6) == 0) {
        int8_t not_found;           /* D4b */

        /* 0x00E5A4CE - 0x00E5A4DE: the unit is widened to a longword */
        route_$port_t *existing =
            ROUTE_$FIND_PORTP((uint16_t)port_type, (int32_t)(int16_t)unit);

        /* 0x00E5A4E0  cmpa.w #0x0,A0 / seq D4b -- a Domain boolean */
        not_found = (existing == NULL) ? (int8_t)-1 : (int8_t)0;

        /* 0x00E5A4E6  tst.b D4b / bpl -- taken when the port already exists */
        if (not_found >= 0) {
            /* 0x00E5A4F6  move.l #0x2b0009,(A0) */
            *status_ret = status_$net_io_illegal_op_for_port_type;
            /* 0x00E5A4FC  bra.w 0x00e5a6b0 */
            return result;
        }
    }

    /* 0x00E5A4EE  clr.l (A0) */
    *status_ret = status_$ok;

    /*
     * 0x00E5A500 - 0x00E5A524: the port that matches the recorded network
     * boot device becomes port 0, the primary network port.  So does any
     * hardware port (types other than 1 and 2) when no boot device was
     * recorded (boot_unit still 999) and port 0 is still free -- 0xE2E0CC is
     * ROUTE_$PORT_ARRAY[0].active.
     */
    if ((port_type == (int16_t)NET_IO_UNWIRED.boot_port_type &&
         unit == NET_IO_UNWIRED.boot_unit) ||
        (NET_IO_UNWIRED.boot_unit == NET_IO_$NO_BOOT_UNIT &&
         ROUTE_$PORT_ARRAY[0].active == 0 &&
         ((1 << (port_type & 0x1F)) & 6) == 0)) {
        /* 0x00E5A522  clr.w D4w */
        port_index = 0;
    } else {
        int16_t candidate;          /* D1w */

        /* 0x00E5A526  move.l #0x2b0005,(A0) */
        *status_ret = status_$net_io_max_ports_open;

        /*
         * 0x00E5A52C - 0x00E5A54E: entries 1 .. 7 of ROUTE_$PORTP (the base
         * is loaded then advanced by one entry before the first test, and
         * the dbf counter is 6).  A free entry is one whose port record has
         * active == 0.
         */
        candidate = 1;
        for (i = 0; i <= 6; i++) {
            if (ROUTE_$PORTP[candidate]->active == 0) {
                /* 0x00E5A540 - 0x00E5A546 */
                port_index = candidate;
                *status_ret = status_$ok;
                break;
            }
            candidate = (int16_t)(candidate + 1);
        }

        /* 0x00E5A552  tst.l (A0) / bne.w 0x00e5a6b0 */
        if (*status_ret != status_$ok) {
            return result;
        }
    }

    /* 0x00E5A55C  move.w D4w,D5w / ext.l D5 */
    port_index_l = (int32_t)port_index;

    /* 0x00E5A55E - 0x00E5A570  A2 = ROUTE_$PORTP[D5], A3 = A2 */
    port = ROUTE_$PORTP[port_index_l];
    port_bytes = (uint8_t *)port;

    port->network = 0;                                  /* 0x00E5A572 clr.l (A2) */
    port->port_type = (uint16_t)port_type;              /* 0x00E5A574 */
    port->socket = unit;                                /* 0x00E5A578 */
    port->driver_info = ARCH_PTR_TO_VA(driver);         /* 0x00E5A57C */

    /*
     * 0x00E5A580 - 0x00E5A594: three longwords route/route.h still carries
     * as route_$port_t._unknown2, plus the port's creation time.  The store
     * at +0x54 and the one at +0x58 (forward_count) are separate clears.
     */
    *(uint32_t *)(port_bytes + 0x4C) = 2;               /* 0x00E5A582 */
    *(uint32_t *)(port_bytes + 0x54) = 0;               /* 0x00E5A586 */
    port->forward_count = 0;                            /* 0x00E5A58A clr.l (0x58,A2) */
    *(uint32_t *)(port_bytes + 0x50) = TIME_$CURRENT_CLOCKH;  /* 0x00E5A58E */

    /* 0x00E5A596 - 0x00E5A5A4: switch on the port type */
    if (port_type == 1) {
        /*
         * 0x00E5A5A8: a local port has no socket of its own, so the socket
         * word is made to hold the port index instead.
         */
        port->socket = (uint16_t)port_index;

        /* 0x00E5A5AC - 0x00E5A5B0  move.w PROC1_$AS_ID,(0x0,A5,D0*0x1) */
        NET_IO_UNWIRED.port_asid[port_index_l] = PROC1_$AS_ID;

        goto set_cleanup;                               /* 0x00E5A5B8 */
    }

    if (port_type != 2) {
        goto check_status;                              /* 0x00E5A5A4 */
    }

    /* 0x00E5A5BC  move.l #0x2b000f,(A0) */
    *status_ret = status_$net_io_max_user_ports_open;

    /*
     * 0x00E5A5C2 - 0x00E5A5E6: find a free ROUTE_$USER_STAT record.  The base
     * is advanced by one record before the first test and every test reads
     * (-0x90,A0), so the record number D1 ends on is 1-based.  Byte 0 of a
     * record is its "in use" Domain boolean.
     */
    stat_index = 0;
    {
        int16_t  candidate = 1;                         /* D1w */
        uint8_t *rec = (uint8_t *)ROUTE_$USER_STAT;     /* A0 */

        rec += 0x90;                                    /* 0x00E5A5CC */
        for (i = 0; i <= 3; i++) {
            /* 0x00E5A5D0  tst.b (-0x90,A0) / bmi */
            if ((int8_t)rec[-0x90] >= 0) {
                /* 0x00E5A5D6 - 0x00E5A5DC */
                stat_index = candidate;
                *status_ret = status_$ok;
                break;
            }
            candidate = (int16_t)(candidate + 1);       /* 0x00E5A5E0 */
            rec += 0x90;                                /* 0x00E5A5E2 */
        }
    }

    /* 0x00E5A5EA  tst.l (A0) / bne.b 0x00e5a620 */
    if (*status_ret == status_$ok) {
        int8_t allocated;                               /* D0b */

        /*
         * 0x00E5A5F2 - 0x00E5A5F6: a longword store at port + 0x34.  Its low
         * half is the word route/route.h calls route_$port_t.socket2, so the
         * queue length lands there and the word at 0x34 is zeroed.
         */
        *(uint32_t *)(port_bytes + 0x34) = (uint32_t)queue_length;

        /*
         * 0x00E5A5FA - 0x00E5A60E: SOCK_$ALLOCATE(&port->socket,
         * (queue_length << 16) | queue_length,
         * (queue_length << 16) | 0x400).  The pushes are, in stack order,
         * the port's socket cell, then three copies of the queue length,
         * then 0x400; sock/sock.h explains how the two longwords are split
         * into queue depth, header pages, data pages and max data length.
         */
        allocated = SOCK_$ALLOCATE(&port->socket,
                                   ((uint32_t)queue_length << 16) |
                                       (uint32_t)queue_length,
                                   ((uint32_t)queue_length << 16) | 0x400);

        /* 0x00E5A612  tst.b D0b / bmi -- a Domain boolean, true is negative */
        if (allocated >= 0) {
            /* 0x00E5A61A  move.l #0x2b000b,(A0) */
            *status_ret = status_$net_io_no_user_buffer_queues;
        }
    }

    /* 0x00E5A620  tst.l (A0) / bne.b 0x00e5a69e */
    if (*status_ret == status_$ok) {
        sock_$sock_t *sock;                             /* A4 */
        uint8_t      *stat_rec;                         /* A1 */
        uint32_t      d0;
        uint32_t      d1;
        int16_t       byte_index;                       /* D0w */
        int16_t       counter;                          /* D1w */

        /*
         * 0x00E5A628 - 0x00E5A63C: the socket pointer table is indexed from
         * 0xE28DB4 with an offset of -4, i.e. entry (socket - 1) of
         * SOCK_$EVENT_COUNTERS.  "bclr.b #0x7,(0x16,A4)" clears bit 7 of the
         * high byte of sock_$sock_t.flags, which is bit 15 of the word.
         */
        sock = (sock_$sock_t *)SOCK_$EVENT_COUNTERS[port->socket - 1];
        sock->flags = (uint16_t)(sock->flags & (uint16_t)~0x8000u);

        /* 0x00E5A642  pea (0x38,A3) / jsr EC_$INIT */
        EC_$INIT((ec_$eventcount_t *)port->port_ec);

        /*
         * 0x00E5A64E - 0x00E5A664: record address = ROUTE_$USER_STAT +
         * n*0x90 - 0x90, with the multiply built as (n<<4) + ((n<<4)<<3).
         */
        d0 = (uint32_t)(int32_t)stat_index;             /* 0x00E5A656 ext.l D0 */
        d0 <<= 4;                                       /* 0x00E5A658 */
        d1 = d0 << 3;                                   /* 0x00E5A65C */
        d0 = d0 + d1;                                   /* 0x00E5A65E */
        stat_rec = (uint8_t *)ROUTE_$USER_STAT + d0;    /* 0x00E5A660 */
        stat_rec -= 0x90;                               /* 0x00E5A664 */

        /* 0x00E5A668  move.l A1,(0x44,A3) */
        port->driver_stats = ARCH_PTR_TO_VA(stat_rec);

        /*
         * 0x00E5A66C - 0x00E5A67E: clear the record.
         *
         *   00e5a66c  move.w  #0x90,D1w
         *   00e5a670  clr.w   D0w
         *   00e5a672  movea.l A0,A0          ; no-op left by the compiler
         *   00e5a674  movea.l (0x44,A3),A1   ; reloaded every iteration
         *   00e5a678  clr.b   (0x0,A1,D0w*0x1)
         *   00e5a67c  addq.w  #0x1,D0w
         *   00e5a67e  dbf     D1w,0x00e5a674
         *
         * ORIGINAL DEFECT (reproduced, not fixed - bead source-2km0): a dbf
         * seeded with 0x90 runs 0x91 times, so this writes offsets 0x00
         * through 0x90 inclusive while the record stride is 0x90.  It clears
         * the first byte of the FOLLOWING record, which for record 4 is the
         * first byte of ROUTE_$PID (0xE88216) - the pid PROC1_$CREATE_P
         * returned in ROUTE_$INIT_ROUTING (0x00E69D60).
         */
        byte_index = 0;
        for (counter = 0x90; counter >= 0; counter--) {
            uint8_t *rec = (uint8_t *)ARCH_VA_TO_PTR(port->driver_stats);

            rec[byte_index] = 0;
            byte_index = (int16_t)(byte_index + 1);
        }

        /* 0x00E5A682  st (A1) -- mark the record in use */
        ((uint8_t *)ARCH_VA_TO_PTR(port->driver_stats))[0] = 0xFF;

        /* 0x00E5A684 - 0x00E5A688 */
        NET_IO_UNWIRED.port_asid[port_index_l] = PROC1_$AS_ID;

        /* 0x00E5A688 falls straight into 0x00E5A690 in the image */
        goto set_cleanup;
    }

    goto check_status;

set_cleanup:
    /* 0x00E5A690  move.w #0xa,-(SP) / jsr PROC2_$SET_CLEANUP */
    PROC2_$SET_CLEANUP(NET_IO_$CLEANUP_CLASS);

check_status:
    /* 0x00E5A69E  tst.l (A0) / bne.b 0x00e5a6b0 */
    if (*status_ret == status_$ok) {
        /* 0x00E5A6A6  move.w #0x1,(0x2c,A2) */
        port->active = 1;
        /* 0x00E5A6AC  move.w D4w,(-0x8,A6) */
        result = port_index;
    }

    /* 0x00E5A6B0  move.w (-0x8,A6),D0w */
    return result;
}
