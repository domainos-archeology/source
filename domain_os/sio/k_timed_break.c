/*
 * SIO_$K_TIMED_BREAK - Hold a break on a line for a number of milliseconds
 *
 * Raises break through sio_$set_break, waits *duration_ptr milliseconds
 * (TIME_$WAIT2 with the quit eventcount as the extra eventcount), and
 * drops break again.  A quit during the wait sets
 * status_$async_fault_while_waiting_for_input (0xB0006) and resyncs the
 * quit value; the break is still dropped.
 *
 * Original address: 0x00E67EE0, 220 bytes (SAU2 map: SIO module at
 * 0xE67D9C).  A5 = 0xE82458 = SIO_$SPIN_LOCK for the nested
 * sio_$set_break (0x00E67E86), which locks through `pea (A5)`.
 *
 * Frame: -0xC..-0x7 the 48-bit delay (word 0 then ms*250 as a longword),
 *        -0x18 the quit wait value.
 *
 *   00e67ee0    link.w A6,-0x18
 *   00e67ee4    movem.l {A5 A3 A2 D2},-(SP)
 *   00e67ee8    lea (0xe82458).l,A5            ; SIO_$SPIN_LOCK
 *   00e67eee    movea.l (0x10,A6),A3           ; arg 3 status_ret
 *   00e67ef2    subq.l #0x2,SP ; pea (A3) ; move.w (*line_ptr),-(SP)
 *   00e67efc    jsr 0x00e667c6.l               ; SIO_$I_GET_DESC -> A0
 *   00e67f06    tst.l (A3) ; bne exit
 *   00e67f0c    subq.l #0x2,SP ; st -(SP) ; pea (A2)
 *   00e67f14    bsr.w 0x00e67e86               ; sio_$set_break(desc, true)
 *   00e67f1a    tst.l (A3) ; bne exit          ; (nothing on that path stores it)
 *   00e67f20    clr.w (-0xc,A6)
 *   00e67f24    move.w (*duration_ptr),D0w ; mulu.w #0xfa,D0
 *   00e67f2e    move.l D0,(-0xa,A6)            ; delay = ms * 250 ticks of 4us
 *   00e67f32    pea (A3)                       ; arg5 status
 *   00e67f34    FIM_$WIRED_DATA.quit_value[asid] + 1 -> (-0x18,A6) ; pea (-0x18,A6)   ; arg4
 *   00e67f50    pea &FIM_$WIRED_DATA.quit_ec[asid]        ; arg3
 *   00e67f68    pea (-0xc,A6)                  ; arg2 &delay
 *   00e67f6c    pea (0x4e,PC)                  ; arg1 &0x00E67FBC (word 0: relative)
 *   00e67f70    jsr 0x00e16654.l               ; TIME_$WAIT2
 *   00e67f7a    tst.b D0b ; bpl 0x00e67fa8     ; true = the quit ec fired
 *   00e67f7e    move.l #0xb0006,(A3)
 *   00e67f84    FIM_$WIRED_DATA.quit_value[asid] = FIM_$WIRED_DATA.quit_ec[asid].value
 *   00e67fa8    subq.l #0x2,SP ; clr.w -(SP) ; pea (A2)
 *   00e67fae    bsr.w 0x00e67e86               ; sio_$set_break(desc, false)
 *   00e67fb2    movem.l (-0x28,A6),{D2 A2 A3 A5} ; unlk ; rts
 */

#include "sio/sio_internal.h"

/* 0x00E67FBC: 00 00 - TIME_$WAIT2's delay-type word (0 = relative). */
static const uint16_t sio_$wait2_relative = 0;

void SIO_$K_TIMED_BREAK(int16_t *line_ptr, uint16_t *duration_ptr,
                        status_$t *status_ret)
{
    sio_desc_t *desc;
    clock_t delay;                  /* (-0xC,A6) */
    uint32_t ticks;
    uint32_t quit_wait_value;       /* (-0x18,A6) */
    int16_t as_id;

    /* 0x00E67EF2-0x00E67F08 */
    desc = SIO_$I_GET_DESC(*line_ptr, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /* 0x00E67F0C-0x00E67F1C */
    sio_$set_break(desc, true);
    if (*status_ret != status_$ok) {
        return;
    }

    /*
     * 0x00E67F20-0x00E67F2E: `clr.w` the top word, then the 32-bit product
     * into the bottom four bytes of the six-byte clock - i.e. high =
     * product >> 16, low = product & 0xFFFF.
     */
    ticks = (uint32_t)*duration_ptr * 0xFAu;
    delay.high = ticks >> 16;
    delay.low = (uint16_t)ticks;

    /* 0x00E67F32-0x00E67F76 */
    as_id = (int16_t)PROC1_$AS_ID;
    quit_wait_value = (uint32_t)FIM_$WIRED_DATA.quit_value[as_id] + 1;
    if (TIME_$WAIT2((uint16_t *)&sio_$wait2_relative, &delay,
                    &FIM_$WIRED_DATA.quit_ec[as_id], &quit_wait_value, status_ret) < 0) {
        /* 0x00E67F7E-0x00E67FA2 */
        *status_ret = status_$async_fault_while_waiting_for_input;
        as_id = (int16_t)PROC1_$AS_ID;
        FIM_$WIRED_DATA.quit_value[as_id] = FIM_$WIRED_DATA.quit_ec[as_id].value;
    }

    /* 0x00E67FA8-0x00E67FAE */
    sio_$set_break(desc, 0);
}
