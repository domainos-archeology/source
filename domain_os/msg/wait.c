/*
 * MSG_$WAIT, MSG_$WAITI - Wait for a message on a socket
 *
 * MSG_$WAITI blocks the caller until a message is queued on the socket, the
 * caller-supplied timeout expires, or a quit fault is delivered.  MSG_$WAIT is
 * the two-argument boolean wrapper that the TRAP #2 dispatcher entry 0x15
 * calls; it allocates the status_$t on its own stack frame and only reports
 * success/failure.
 *
 * Original addresses:
 *   MSG_$WAIT:  0x00E59BA4 (28 bytes)
 *   MSG_$WAITI: 0x00E59BC0 (282 bytes)
 * Both in the map's MSG_UNWIRED object at 0xE5911C (size 0x100C).
 * Re-verified instruction by instruction against 0x00E59BA4-0x00E59BBE and
 * 0x00E59BC0-0x00E59CD8: the two-argument SVC form with its local status
 * and `seq D0b`, the zero-extended timeout word, the three-way EC_$WAIT and
 * its 0/1/2/other dispatch (other leaves the status untouched).
 */

#include "msg/msg_internal.h"
#include "fim/fim.h"
#include "time/time.h"

/*
 * MSG_$WAITI - Wait for a message (internal implementation)
 *
 * timeout points at a 16-bit tick count: the original only reads a word from
 * it (0x00E59C4C "clr.l D1" / 0x00E59C4E "move.w (A4),D1w") and adds it to
 * TIME_$CLOCKH to form the deadline handed to EC_$WAIT.
 *
 * 0x00E59BC0  link.w A6,-0xc          locals: -0x8 sock_target, -0x4 quit_target
 * 0x00E59BC8  lea (0xe80d84).l,A5     A5 = &MSG_$UNWIRED_DATA
 * 0x00E59BCE  movea.l (0x8,A6),A0     socket
 * 0x00E59BD2  movea.l (0x10,A6),A2    status_ret
 */
void MSG_$WAITI(msg_$socket_t *socket, int16_t *timeout, status_$t *status_ret)
{
    int16_t sock_num;
    uint16_t asid;
    int16_t byte_index;
    sock_$sock_t *sock;          /* the socket descriptor / its event count */
    int32_t sock_target;         /* wait value for the socket event count */
    int32_t quit_target;         /* wait value for the quit event count */
    int16_t result;

    /* 0x00E59BD6 move.w (A0),D0w */
    sock_num = (int16_t)*socket;

    /*
     * Validate the socket number.
     * 0x00E59BD8  ble.b   -> out of range if <= 0
     * 0x00E59BDA  cmpi.w #0xe0,D0w / ble.b -> in range if <= 224
     * 0x00E59BE0  move.l #0x290001,(A2)
     */
    if (sock_num <= 0 || sock_num > MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;
        return;
    }

    /*
     * Ownership check.  The bitmap byte is (0x3F - asid) >> 3 computed in
     * word arithmetic with a *logical* shift, and the addressing mode
     * sign-extends that word:
     *   0x00E59BF0  moveq #0x3f,D0
     *   0x00E59BF2  move.w (0x00e2060a).l,D1w   D1 = PROC1_$AS_ID
     *   0x00E59BF8  sub.w D1w,D0w
     *   0x00E59BFA  lsr.w #0x3,D0w
     *   0x00E59C00  btst.b D1,(0x0,A1,D0w*0x1)  bit number is asid mod 8
     *   0x00E59C06  move.l #0x290005,(A2)
     */
    asid = PROC1_$AS_ID;
    byte_index = (int16_t)((uint16_t)(0x3F - asid) >> 3);

    if ((MSG_$UNWIRED_DATA.ownership[sock_num][byte_index] & (1 << (asid & 7))) == 0) {
        *status_ret = status_$msg_no_owner;
        return;
    }

    /*
     * The socket's event count doubles as its descriptor.
     *   0x00E59C10  move.w (A0),D2w
     *   0x00E59C12  movea.l #0xe28db4,A1
     *   0x00E59C18  lsl.w #0x2,D2w
     *   0x00E59C1A  lea (0x0,A1,D2w*0x1),A3
     *   0x00E59C1E  movea.l (-0x4,A3),A3
     * i.e. *(0xE28DB0 + socket*4) == SOCK_$DATA.socket_ptr[socket].
     */
    sock = SOCK_$DATA.socket_ptr[sock_num];

    /*
     * Both wait targets are computed before the "message already queued" test
     * in the original (0x00E59C22 / 0x00E59C32).
     */
    sock_target = sock->ec.value + 1;               /* 0x00E59C22, 0x00E59C24 */
    quit_target = (int32_t)FIM_$WIRED_DATA.quit_value[asid] + 1; /* 0x00E59C32, 0x00E59C36 */

    /*
     * A packet is already queued -> return immediately.
     *   0x00E59C3C  clr.w D1w
     *   0x00E59C3E  move.b (0x15,A3),D1b     sock_$sock_t.queue_count
     *   0x00E59C42  tst.w D1w
     *   0x00E59C44  bne.b -> 0x00E59C96 "clr.l (A2)"
     */
    if (sock->queue_count != 0) {
        *status_ret = status_$ok;
        return;
    }

    /*
     * Wait on the socket, the clock and the quit event count
     * (0x00E59C46 - 0x00E59C7A).  Arguments are pushed right-to-left, so the
     * values go down first and the pointers end up at the lower addresses:
     *   0x00E59C46  move.l D0,-(SP)              vals[2] = FIM_$WIRED_DATA.quit_value[asid]+1
     *   0x00E59C56  move.l D1,-(SP)              vals[1] = TIME_$CLOCKH + *timeout
     *   0x00E59C58  move.l D2,-(SP)              vals[0] = sock->ec.value + 1
     *   0x00E59C6E  pea (0x0,A1,D3w*0x1)         ecs[2] = &FIM_$WIRED_DATA.quit_ec[asid]
     *                                            (A1 = 0xE22002, D3 = asid*12)
     *   0x00E59C72  move.l #0xe2b0d4,-(SP)       ecs[1] = &TIME_$CLOCKH
     *   0x00E59C78  pea (A3)                     ecs[0] = the socket
     *   0x00E59C80  lea (0x18,SP),SP             24 bytes: two 3-element arrays
     * EC_$WAIT returns the 0-based index of the satisfied event count in D0.w.
     */
    result = EC_$WAIT(
        (ec_$wait_ecs_t){{ &sock->ec,
                           (ec_$eventcount_t *)&TIME_$CLOCKH,
                           &FIM_$WIRED_DATA.quit_ec[asid] }},
        (ec_$wait_vals_t){{ sock_target,
                            (int32_t)(TIME_$CLOCKH + (uint32_t)(uint16_t)*timeout),
                            quit_target }});

    /*
     * 0x00E59C86  beq.b  -> 0x00E59C96  clr.l (A2)
     * 0x00E59C8C  beq.b  -> 0x00E59C9A  status_$msg_time_out
     * 0x00E59C92  beq.b  -> 0x00E59CA2  quit
     * 0x00E59C94  bra.b  -> 0x00E59CD0  (any other index leaves *status_ret
     *                                    untouched - preserved deliberately)
     */
    if (result == 0) {
        *status_ret = status_$ok;
    } else if (result == 1) {
        *status_ret = status_$msg_time_out;
    } else if (result == 2) {
        /*
         * 0x00E59CC4  move.l (0x0,A1,D1w*0x1),(0x0,A0,D0w*0x1)
         *   A1 = 0xE22002 (FIM_$QUIT_EC, 12 bytes each, D1 = asid*12)
         *   A0 = 0xE222BA (FIM_$QUIT_VALUE, 4 bytes each, D0 = asid*4)
         */
        FIM_$WIRED_DATA.quit_value[asid] = (uint32_t)FIM_$WIRED_DATA.quit_ec[asid].value;
        *status_ret = status_$msg_quit_fault;
    }
}

/*
 * MSG_$WAIT - boolean wrapper around MSG_$WAITI (SVC TRAP #2 entry 0x15)
 *
 * The original takes exactly two arguments and keeps the status in its own
 * frame; it never writes a caller-supplied status_$t:
 *   0x00E59BA4  link.w A6,-0x8            status_$t at (-0x4,A6)
 *   0x00E59BA8  pea (-0x4,A6)             &status
 *   0x00E59BAC  move.l (0xc,A6),-(SP)     timeout
 *   0x00E59BB0  move.l (0x8,A6),-(SP)     socket
 *   0x00E59BB4  bsr.b 0x00e59bc0          MSG_$WAITI
 *   0x00E59BB6  tst.l (-0x4,A6)
 *   0x00E59BBA  seq D0b                   0xFF when the status is status_$ok
 *   0x00E59BBC  unlk A6 / rts
 */
boolean MSG_$WAIT(msg_$socket_t *socket, int16_t *timeout)
{
    status_$t status;

    MSG_$WAITI(socket, timeout, &status);

    return (status == status_$ok) ? true : false;
}
