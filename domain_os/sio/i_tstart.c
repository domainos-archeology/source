/*
 * SIO_$I_TSTART - Start/continue transmission
 *
 * Main transmit state machine.  Pulls characters from the transmit buffer and
 * sends them to the output device, handling the two escape sequences the
 * buffer can carry and the XON/XOFF break handshake.
 *
 * Transmit buffer format:
 *   - Normal characters are sent directly
 *   - 0xFE followed by 0x00 followed by 2 bytes = a timed delay in
 *     milliseconds
 *   - 0xFE followed by a non-zero byte: that byte is sent as a normal
 *     character (0x00E1C856 falls into the ordinary output path)
 *
 * Original address: 0x00e1c7a8 (418 bytes)
 *
 * Assembly:
 *   00e1c7a8  link.w A6,-0x28
 *   00e1c7b0  lea (0xe2dddc).l,A5        ; SIO module data base
 *   00e1c7b6  moveq #0x1f,D0 / and.w (0x74,A0),D0w / bne -> return
 *   00e1c7c4  moveq #0x60,D0 / and.w (0x74,A0),D0w / beq -> 0x00e1c812
 *   00e1c7cc  btst.b #0x6,(0x75,A0)      ; state & 0x0040
 *   00e1c7d4  output_char(desc->context, 0x13) ; XOFF
 *   00e1c7e6  bset.b #0x7,(0x75,A0) / bclr.b #0x6,(0x75,A0)
 *   00e1c7f6  output_char(desc->context, 0x11) ; XON
 *   00e1c808  andi.w #-0xa1,(0x74,A0)    ; state &= 0xFF5F, i.e. clears 0x00A0
 *   00e1c812  movea.l (0x24,A0),A2       ; txbuf
 *   00e1c816  move.w (A2),D0w / cmp.w (0x2,A2),D0w / beq -> return
 *   00e1c820  bset.b #0x0,(0x75,A0)      ; state |= 0x0001 (transmit active)
 *   00e1c826  move.b (0x5,A2,D0w*0x1),D2b
 *   00e1c838  cmpi.b #-0x2,D2b / bne -> 0x00e1c90e
 *   00e1c840..00e1c856                   ; second byte; non-zero -> send it
 *   00e1c85a  clr.l (-0x14,A6) / clr.w (-0x10,A6)
 *   00e1c862  moveq #0x1,D0              ; dbf -> exactly two iterations
 *   00e1c864  move.l (-0x12,A6),D1 / lsl.l #0x8,D1 / add.l D4,D1
 *   00e1c88a  M$MIU$LLW(D1, 0xfa)        ; milliseconds -> 4us ticks
 *   00e1c89a  move.l D0,(-0x12,A6)
 *   00e1c89e  moveq #-0x2,D0 / and.w (0x74,A0),D0w / ori.w #0x10,D0w
 *   00e1c8b0  pea (-0xc,A6) / jsr TIME_$ABS_CLOCK
 *   00e1c8bc..00e1c8e6  TIME_$Q_ADD_CALLBACK, ten arguments right to left
 *   00e1c8f0  tst.l (-0x18,A6) / beq -> return
 *   00e1c8f6..00e1c908  build the two-level callback argument, call directly
 *   00e1c90e  output_char(desc->context, char) ; the ordinary path
 *   00e1c91c  D0 = write_idx - read_idx, + size if negative
 *   00e1c928  cmpi.w #0x10 / beq, tst.w / bne -> return
 *   00e1c936  drain_handler(desc->owner)
 *
 * Notes on the frame:
 *   -0x04  the descriptor pointer the direct-restart path publishes
 *   -0x0c  `now`, filled by TIME_$ABS_CLOCK
 *   -0x14  `delay`, the 48-bit expiry handed to the queue.  The accumulator
 *          the delay loop uses is the LONGWORD at -0x12, which straddles the
 *          low half of that value's high longword and all of its low word.
 *   -0x18  the status TIME_$Q_ADD_CALLBACK returns
 *   -0x28  the cell holding &(-0x04)
 *
 * The word at 0x74 is one 16-bit state field; the "byte at 0x75" the original
 * bit instructions address is simply its low byte, so the SIO_XMIT_* names
 * (bits 0..7) and the SIO_STATE_* names (bits 4..7) describe the same word.
 */

#include "sio/sio_internal.h"

/*
 * The `interval` argument is "pea (A5)" at 0x00E1C8C8, i.e. the first six
 * bytes of the SIO module data base at 0x00E2DDDC, which the image holds as
 * zero: a Pascal by-reference constant meaning "one-shot".
 *
 *   gsk read 0x00E2DDDC 16
 *   00e2dddc  00 00 00 00 00 00 00 00  00 01 02 03 12 10 11 0f
 */
static const clock_t sio_$c_zero_interval = { 0, 0 };   /* 0x00E2DDDC */

/*
 * The character codes the break handshake writes.  The original pushes them
 * as "move.w #0x1300,-(SP)" / "move.w #0x1100,-(SP)": a Pascal byte value
 * parameter occupies a two-byte slot with the byte at the EVEN address, which
 * is also where "move.b D2b,-(SP)" puts the ordinary character at
 * 0x00E1C910.
 */
#define SIO_TSTART_XOFF     0x13
#define SIO_TSTART_XON      0x11

/*
 * The output routine is reached through desc->output_char (+0x3C).  Every
 * call site reserves a two-byte Pascal result slot ("subq.l #0x2,SP") and
 * never reads it.
 */
typedef void (*sio_output_char_fn)(uint32_t context, uint8_t ch);

/* desc->drain_handler (+0x2C) is called with desc->owner (+0x04). */
typedef void (*sio_drain_fn)(uint32_t owner);

/*
 * 0xE1C864..0xE1C874: read and write the longword at A6-0x12, which is the
 * bottom 32 bits of the 48-bit value based at A6-0x14.  Doing it through the
 * clock_t fields keeps the code correct on a little-endian host.
 */
static uint32_t sio_delay_low32_get(const clock_t *c)
{
    return ((c->high & 0x0000FFFFu) << 16) | (uint32_t)c->low;
}

static void sio_delay_low32_set(clock_t *c, uint32_t value)
{
    c->high = (c->high & 0xFFFF0000u) | (value >> 16);
    c->low = (uint16_t)value;
}

/*
 * SIO_DELAY_RESTART - Callback from the time subsystem when a delay expires
 *
 * Original address: 0x00e1c690
 *
 * Assembly:
 *   00e1c696  movea.l (0x8,A6),A1        ; arg
 *   00e1c69c  movea.l (A1),A0            ; *arg
 *   00e1c69e  move.l (A0),D0             ; **arg = the descriptor
 *   00e1c69a  moveq #-0x12,D1
 *   00e1c6a2  and.w D1w,(0x74,A2)        ; state &= 0xFFEE - clears 0x11
 *   00e1c6a6  pea (A2) / bsr SIO_$I_TSTART
 *
 * The mask clears BOTH the delay-active bit (0x10) and the transmit-active
 * bit (0x01), not just the former.
 */
void SIO_DELAY_RESTART(time_$callback_arg_t arg)
{
    sio_desc_t *desc;

    /* 0xE1C69C/0xE1C69E: two levels of indirection to the callback argument */
    desc = (sio_desc_t *)ARCH_VA_TO_PTR(**arg);

    /* 0xE1C6A2 */
    desc->state &= (uint16_t)~(SIO_STATE_DELAY_ACTIVE | SIO_XMIT_ACTIVE);

    /* 0xE1C6A8 */
    SIO_$I_TSTART(desc);
}

void SIO_$I_TSTART(sio_desc_t *desc)
{
    sio_txbuf_t *txbuf;
    uint16_t read_idx;
    uint16_t pending;
    uint8_t char_data;
    status_$t status;
    clock_t now;                /* A6-0x0C */
    clock_t delay;              /* A6-0x14 */
    uint32_t delay_value;
    uint32_t desc_cell;         /* A6-0x04 */
    uint32_t *desc_cell_ptr;    /* A6-0x28 */
    int i;

    /*
     * 0xE1C7B6: any of bits 0..4 blocks transmission (transmit already
     * active, CTS blocked, inhibited, break in progress, delay running).
     */
    if ((desc->state & 0x001F) != 0) {
        return;
    }

    /* 0xE1C7C4: the XON/XOFF handshake bits */
    if ((desc->state & 0x0060) != 0) {
        if ((desc->state & SIO_XMIT_DEFER_PENDING) != 0) {
            /* 0xE1C7D4 */
            ((sio_output_char_fn)ARCH_VA_TO_PTR(desc->output_char))(desc->context,
                                                    SIO_TSTART_XOFF);
            /* 0xE1C7E6 */
            desc->state |= SIO_XMIT_DEFER_COMPLETE;
            desc->state &= (uint16_t)~SIO_XMIT_DEFER_PENDING;
        } else {
            /* 0xE1C7F6 */
            ((sio_output_char_fn)ARCH_VA_TO_PTR(desc->output_char))(desc->context,
                                                    SIO_TSTART_XON);
            /*
             * 0xE1C808 "andi.w #-0xa1,(0x74,A0)": the immediate is 0xFF5F, so
             * this clears 0x0020 and 0x0080 - NOT the whole 0x60 pair.
             */
            desc->state &= (uint16_t)~(SIO_XMIT_DEFER_INHIBIT |
                                       SIO_XMIT_DEFER_COMPLETE);
        }
        return;
    }

    /* 0xE1C812 */
    txbuf = (sio_txbuf_t *)ARCH_VA_TO_PTR(desc->txbuf);
    read_idx = txbuf->read_idx;

    /* 0xE1C818: nothing queued */
    if (read_idx == txbuf->write_idx) {
        return;
    }

    /* 0xE1C820 */
    desc->state |= SIO_XMIT_ACTIVE;

    /* 0xE1C826: the buffer is Pascal 1-based, element i at txbuf+0x05+i */
    char_data = txbuf->data[read_idx - 1];

    /* 0xE1C82A: advance the circular read index */
    if (read_idx == txbuf->size) {
        txbuf->read_idx = 1;
    } else {
        txbuf->read_idx = (uint16_t)(read_idx + 1);
    }

    /* 0xE1C838 */
    if (char_data == SIO_TSTART_DELAY_MARKER) {
        /* 0xE1C840: the byte after the marker */
        read_idx = txbuf->read_idx;
        char_data = txbuf->data[read_idx - 1];

        if (read_idx == txbuf->size) {
            txbuf->read_idx = 1;
        } else {
            txbuf->read_idx = (uint16_t)(read_idx + 1);
        }

        /*
         * 0xE1C854: a non-zero command byte is not an escape at all - the
         * branch falls into the ordinary output path with that byte in D2.
         */
        if (char_data == SIO_TSTART_DELAY_CMD) {
            /* 0xE1C85A */
            delay.high = 0;
            delay.low = 0;

            /* 0xE1C862: moveq #0x1 + dbf, i.e. exactly two bytes */
            for (i = 0; i < 2; i++) {
                delay_value = sio_delay_low32_get(&delay);

                read_idx = txbuf->read_idx;
                delay_value = (delay_value << 8) +
                              (uint32_t)txbuf->data[read_idx - 1];

                sio_delay_low32_set(&delay, delay_value);

                if (read_idx == txbuf->size) {
                    txbuf->read_idx = 1;
                } else {
                    txbuf->read_idx = (uint16_t)(read_idx + 1);
                }
            }

            /*
             * 0xE1C88A: the count is in milliseconds and the clock counts
             * 4us ticks, so 250 ticks per millisecond.  D1 - the register,
             * not the memory copy - is what is multiplied.
             */
            delay_value = M$MIU$LLW(delay_value, 0xFA);
            sio_delay_low32_set(&delay, delay_value);

            /*
             * 0xE1C89E: clear the transmit-active bit and set the
             * delay-active bit in one write.
             */
            desc->state = (uint16_t)((desc->state & 0xFFFE) |
                                     SIO_STATE_DELAY_ACTIVE);

            /* 0xE1C8B0 */
            TIME_$ABS_CLOCK(&now);

            /*
             * 0xE1C8BC..0xE1C8E6.  `when` is the computed delay, taken as
             * relative because the is_absolute word at 0xE1C8DA is zero, and
             * the queue element is the descriptor's own (0xE1C8C4
             * "pea (0x8,A0)").
             */
            TIME_$Q_ADD_CALLBACK(&TIME_$RTEQ,
                                 &delay,
                                 0,
                                 &now,
                                 (void *)SIO_DELAY_RESTART,
                                 desc,
                                 8,
                                 (clock_t *)&sio_$c_zero_interval,
                                 &desc->delay_qelem,
                                 &status);

            /* 0xE1C8F0: if it could not be queued, run the callback now */
            if (status != status_$ok) {
                /*
                 * 0xE1C8F6..0xE1C904 build the same two-level chain
                 * TIME_$Q_SCAN_QUEUE would have passed.
                 */
                desc_cell = ARCH_PTR_TO_VA(desc);
                desc_cell_ptr = &desc_cell;
                SIO_DELAY_RESTART(&desc_cell_ptr);
            }

            return;
        }
    }

    /* 0xE1C90E: ordinary character */
    ((sio_output_char_fn)ARCH_VA_TO_PTR(desc->output_char))(desc->context, char_data);

    /*
     * 0xE1C91C: how much is still queued, wrapping through the buffer size.
     * The subtraction is signed ("bpl").
     */
    pending = (uint16_t)(txbuf->write_idx - txbuf->read_idx);
    if ((int16_t)pending < 0) {
        pending = (uint16_t)(pending + txbuf->size);
    }

    /* 0xE1C928: exactly 0x10 left, or empty, wakes the writer */
    if (pending == 0x10 || pending == 0) {
        ((sio_drain_fn)ARCH_VA_TO_PTR(desc->drain_handler))(desc->owner);
    }
}
