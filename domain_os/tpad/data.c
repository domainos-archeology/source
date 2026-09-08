/*
 * tpad/data.c - TPAD_$DATA (0x00E691BC, 1538 bytes)
 *
 * Process one pointing-device data packet and update the cursor.
 *
 * The whole body (0x00E691BC-0x00E697BC) is re-emitted here as a single
 * function, because the original keeps the two movement deltas in D2/D1 and
 * the acceleration flag in D4 across every section: the mouse arm branches
 * straight into the clamp section with its deltas still live, and the
 * bitpad/touchpad arms fall into a shared tail that recomputes them.
 *
 * Frame: `link.w A6,-0x18` / `movem.l {A5 A2 D6 D5 D4 D3 D2},-(SP)`.
 *   +0x08 packet   longword (A1)
 * A5 = 0xE8245C, the TPAD module data base; the per-unit configuration array
 * starts there and tpad_$globals sits at A5+0x160.  A2 is set to
 * A5 + unit*0x2C (`muls.w (0x17c,A5),D0` / `lea (0x0,A5,D0*0x1),A2` at
 * 0x00E691D0), so every config field is a NEGATIVE displacement off A2 and
 * A2-0x2C is &tpad_$unit_configs[unit - 1].
 *
 * Register map used by the comments below:
 *   D2w  delta_x            D1w  delta_y (and, earlier, device_changed in D1b)
 *   D4b  accel_flag         D0b  edge_flag (clamp section)
 *   D3w  new_button_state (mouse arm)
 *  (-0x2,A6) edge_type
 *
 * Bead source-0ni8: the previous C simplified the 48-bit clock arithmetic at
 * 0x00E69640/0x00E6967E, compared the velocity with `>= 100` where the
 * original uses `ble` (i.e. accelerate only when > 100), and lost the
 * fall-through from the snap-to-horizontal arm (0x00E6967A `clr.w D1w`) into
 * `st D4b` at 0x00E6967C.
 */

#include "tpad/tpad_internal.h"
#include "smd/smd.h"

/*
 * m68k DIVS.W: a 32-bit dividend divided by a 16-bit divisor, 16-bit
 * quotient.  C integer division truncates toward zero, as DIVS.W does.
 * (A zero divisor traps on the original exactly as it would here; the code
 * never guards against it, so neither does this.)
 */
#define TPAD_DIVS_W(dividend, divisor) \
    ((int16_t)((int32_t)(dividend) / (int32_t)(int16_t)(divisor)))

/*
 * The low 32 bits of a 48-bit Domain clock.  The original reads them with a
 * single `move.l` at offset +2 of the 6-byte record (0x00E69640
 * `move.l (0x6,A1),D3` over the packet clock at +4, and 0x00E69644
 * `sub.l (0x166,A5),D3` over tpad_$last_clock at 0x164), i.e. the low half
 * of `high` concatenated with `low`.  Assembled with shifts so a
 * little-endian host gets the same value.
 */
#define TPAD_CLOCK_LOW32(hi32, lo16) \
    ((int32_t)((((uint32_t)(hi32) & 0xFFFFu) << 16) | (uint32_t)(uint16_t)(lo16)))

/*
 * TPAD_$DATA - Process pointing device data packet
 */
void TPAD_$DATA(tpad_$data_packet_t *packet)
{
    tpad_$unit_config_t *config;
    uint16_t dev_id;             /* D0w, `clr.w D0w` then `move.b (0xa,A1),D0b` */
    int16_t delta_x;             /* D2w */
    int16_t delta_y;             /* D1w */
    int8_t accel_flag;           /* D4b */
    int8_t device_changed;       /* D1b in the bitpad/touchpad arms */
    int8_t edge_flag;            /* D0b */
    int16_t edge_type;           /* (-0x2,A6) */
    int32_t acc;                 /* the 32-bit scratch the arms share */
    int16_t v;

    /* 0x00E691C4-0x00E691D4 */
    config = TPAD_$UNIT_CONFIG(tpad_$unit);

    /* 0x00E691D8-0x00E691DC */
    accel_flag = 0;
    dev_id = packet->dev_id;

    /* 0x00E691E0 cmpi.w #0xdf,D0w */
    if (dev_id == TPAD_$MOUSE_ID) {
        /* ---------------- mouse, 0x00E691E8-0x00E69354 ---------------- */
        int16_t button_bits;      /* D0w */
        int16_t new_button_state; /* D3w */
        int16_t dx_raw;           /* D1w, sign-extended packet byte */
        int16_t dy_raw;           /* D5w */

        tpad_$dev_type = tpad_$have_mouse;             /* 0x00E691E8 */

        /* 0x00E691EE-0x00E6920E: 7 - (b&1) - (b&2)*2 - ((b&4)>>1) */
        button_bits = (int16_t)((packet->flags & 0x70) >> 4);
        new_button_state = (int16_t)(7 - (button_bits & 1));
        new_button_state = (int16_t)(new_button_state - ((button_bits & 2) * 2));
        new_button_state = (int16_t)(new_button_state - ((button_bits & 4) >> 1));

        /* 0x00E69210 cmp.w (0x172,A5),D3w / beq / st (0x17e,A5) */
        if (new_button_state != tpad_$button_state) {
            tpad_$re_origin_flag = (int8_t)0xFF;
        }

        /* 0x00E6921A-0x00E69222: X overflow bits */
        if ((packet->flags & 0x03) == 0) {
            /* 0x00E69224-0x00E69242 */
            dx_raw = (int16_t)(int8_t)packet->b0;
            acc = (int32_t)tpad_$accum_x + (int32_t)dx_raw * (int32_t)config->x_scale;
            if (acc < 0) {
                acc += 0x3FF;                          /* 0x00E6923A */
            }
            acc >>= 10;                                /* asr.l #8 / asr.l #2 */
            delta_x = (int16_t)acc;

            /* 0x00E69244-0x00E69256 */
            if (tpad_$re_origin_flag < 0) {
                int16_t mag = delta_x;
                if (mag < 0) {
                    mag = (int16_t)(-mag);
                }
                if (mag < config->hysteresis) {
                    delta_x = 0;
                }
            }

            /*
             * 0x00E69258-0x00E6926C.  Both the *0x400 and the accumulator
             * update are WORD arithmetic (`lsl.w`, `muls.w` then `add.w`),
             * so they truncate to 16 bits exactly as the original does.
             */
            tpad_$accum_x = (int16_t)((int16_t)(dx_raw * config->x_scale) +
                                      tpad_$accum_x -
                                      (int16_t)((uint16_t)delta_x << 10));
            tpad_$delta_x = delta_x;
        } else {
            /* 0x00E69272: overflow - reuse the previous delta, no store */
            delta_x = tpad_$delta_x;
        }

        /* 0x00E69276-0x00E692A0: smoothing when the scale is below 0x400 */
        if (config->x_scale < TPAD_$FACTOR_DEFAULT) {
            uint16_t mag = (uint16_t)delta_x;          /* `clr.l D0` + `move.w` */
            if (delta_x < 0) {
                mag = (uint16_t)(-delta_x);
            }
            acc = M$MIS$LLW((int32_t)(uint32_t)mag + 10, delta_x);
            delta_x = TPAD_DIVS_W(acc, 10);
        }

        /* 0x00E692A2-0x00E692AA: Y overflow bits */
        if (((packet->flags & 0x0C) >> 2) == 0) {
            /* 0x00E692AC-0x00E692CC */
            dy_raw = (int16_t)(int8_t)packet->b1;
            acc = (int32_t)tpad_$accum_y - (int32_t)dy_raw * (int32_t)config->y_scale;
            if (acc < 0) {
                acc += 0x3FF;                          /* 0x00E692C2 */
            }
            acc >>= 10;
            delta_y = (int16_t)acc;

            /* 0x00E692CE-0x00E692E0 */
            if (tpad_$re_origin_flag < 0) {
                int16_t mag = delta_y;
                if (mag < 0) {
                    mag = (int16_t)(-mag);
                }
                if (mag < config->hysteresis) {
                    delta_y = 0;
                }
            }

            /* 0x00E692E2-0x00E692F8: `neg.w` then `add.w`/`sub.w` */
            tpad_$accum_y = (int16_t)((int16_t)(-(int16_t)(dy_raw * config->y_scale)) +
                                      tpad_$accum_y -
                                      (int16_t)((uint16_t)delta_y << 10));
            tpad_$delta_y = delta_y;
        } else {
            delta_y = tpad_$delta_y;                   /* 0x00E692FE */
        }

        /* 0x00E69302-0x00E6932C */
        if (config->y_scale < TPAD_$FACTOR_DEFAULT) {
            uint16_t mag = (uint16_t)delta_y;
            if (delta_y < 0) {
                mag = (uint16_t)(-delta_y);
            }
            acc = M$MIS$LLW((int32_t)(uint32_t)mag + 10, delta_y);
            delta_y = TPAD_DIVS_W(acc, 10);
        }

        /* 0x00E6932E-0x00E69334: D4b is still zero here */
        if (new_button_state == tpad_$button_state) {
            tpad_$re_origin_flag = accel_flag;
        }

        /* 0x00E69338-0x00E69344: nothing changed at all -> return */
        if (new_button_state == tpad_$button_state && delta_x == 0 && delta_y == 0) {
            return;
        }

        /* 0x00E69348-0x00E69354 */
        tpad_$button_state = new_button_state;
        tpad_$cursor_x = (int16_t)(tpad_$cursor_x + delta_x);
        tpad_$cursor_y = (int16_t)(tpad_$cursor_y + delta_y);
        /* branches straight to the clamp section with D2/D1 still live */
    }
    else if (dev_id == TPAD_$BITPAD_ID) {
        /* ---------------- bitpad, 0x00E6935E-0x00E693C4 ---------------- */
        int16_t raw;

        /* 0x00E69360-0x00E69366 */
        device_changed = (tpad_$dev_type != tpad_$have_bitpad) ? (int8_t)0xFF
                                                               : (int8_t)0x00;
        tpad_$dev_type = tpad_$have_bitpad;

        /* 0x00E6936A-0x00E6937A: (b1 << 6) + b0, both zero-extended bytes */
        tpad_$raw_x = (int16_t)(((uint16_t)packet->b1 << 6) + packet->b0);

        /* 0x00E6937E-0x00E6938E: (b3 << 6) + b2 */
        tpad_$raw_y = (int16_t)(((uint16_t)packet->b3 << 6) + packet->b2);

        /* 0x00E69392-0x00E6939E */
        tpad_$raw_x = TPAD_DIVS_W((int32_t)tpad_$raw_x * (int32_t)config->x_scale,
                                  TPAD_$BITPAD_SCALE);

        /* 0x00E693A2-0x00E693B4 */
        raw = TPAD_DIVS_W((int32_t)tpad_$raw_y * (int32_t)config->y_scale,
                          TPAD_$BITPAD_SCALE);
        tpad_$raw_y = (int16_t)(config->y_scale - raw);

        /* 0x00E693B8-0x00E693C0 */
        tpad_$button_state = (int16_t)((packet->flags & 0x3C) >> 2);

        goto relative_tail;                            /* 0x00E693C4 */
    }
    else {
        /* ---------------- touchpad, 0x00E693C8-0x00E695D2 -------------- */
        int16_t raw_x, raw_y;

        /* 0x00E693CA-0x00E693D4 */
        device_changed = (tpad_$dev_type != tpad_$have_touchpad) ? (int8_t)0xFF
                                                                 : (int8_t)0x00;
        tpad_$dev_type = tpad_$have_touchpad;
        tpad_$button_state = 0;

        /* 0x00E693D8-0x00E693E8: ((b0 & 0x0f) << 8) + flags */
        tpad_$raw_x = (int16_t)((uint16_t)((packet->b0 & 0x0F) << 8) + packet->flags);

        /* 0x00E693EC-0x00E69400: (b1 << 4) + ((b0 & 0xf0) >> 4) */
        tpad_$raw_y = (int16_t)(((uint16_t)((packet->b0 & 0xF0) >> 4)) +
                                ((uint16_t)packet->b1 << 4));

        /* 0x00E69404-0x00E69420: inverted orientation */
        if (tpad_$touchpad_max >= TPAD_$TOUCHPAD_INVERTED) {
            tpad_$raw_x = (int16_t)(0xFFF - tpad_$raw_x);
            tpad_$raw_y = (int16_t)(0xFFF - tpad_$raw_y);
        }

        /* 0x00E69424-0x00E69434: out of range leaves the routine entirely */
        if (tpad_$touchpad_max < tpad_$raw_x) {
            return;
        }
        if (tpad_$touchpad_max < tpad_$raw_y) {
            return;
        }

        raw_x = tpad_$raw_x;
        raw_y = tpad_$raw_y;

        /* 0x00E69438-0x00E69444 */
        config->sample_count++;
        if (config->sample_count < TPAD_$RANGING_SAMPLES) {
            /* --- X auto-ranging, 0x00E69448-0x00E694AE --- */
            /* 0x00E69448-0x00E69464: the compare is LONG, the store is WORD */
            if ((int32_t)raw_x + 0x32 < (int32_t)config->x_min) {
                config->x_min = (int16_t)(0x32 + raw_x);
            }
            /* 0x00E69468-0x00E69484 */
            if (((int32_t)raw_x - 0x32) - (int32_t)config->x_min >
                (int32_t)config->x_range) {
                config->x_range = (int16_t)((int16_t)(raw_x - 0x32) - config->x_min);
                /* 0x00E69496-0x00E694AE */
                if (config->x_scale == 0) {
                    config->x_factor = TPAD_$FACTOR_DEFAULT;
                } else {
                    config->x_factor = TPAD_DIVS_W(config->x_range, config->x_scale);
                }
            }

            /* --- Y auto-ranging, 0x00E694B2-0x00E69518 --- */
            if ((int32_t)raw_y + 0x32 < (int32_t)config->y_min) {
                config->y_min = (int16_t)(0x32 + raw_y);
            }
            if (((int32_t)raw_y - 0x32) - (int32_t)config->y_min >
                (int32_t)config->y_range) {
                config->y_range = (int16_t)((int16_t)(raw_y - 0x32) - config->y_min);
                if (config->y_scale == 0) {
                    config->y_factor = TPAD_$FACTOR_DEFAULT;
                } else {
                    config->y_factor = TPAD_DIVS_W(config->y_range, config->y_scale);
                }
            }
        }

        /*
         * 0x00E6951C-0x00E69580: scaled mode with a long enough gap uses the
         * device position absolutely.  The `bls` at 0x00E69522 makes the
         * elapsed-time compare UNSIGNED.
         */
        if (packet->elapsed > 0x1E848u && config->mode == tpad_$scaled) {
            acc = M$MIS$LLW((int32_t)tpad_$raw_x - (int32_t)config->x_min,
                            config->x_max_disp);
            tpad_$cursor_x = (int16_t)((int16_t)(config->x_max_disp + 1) -
                                       TPAD_DIVS_W(acc, config->x_range));

            /* the Y form is M$MIS$LLL: both arguments are longwords */
            acc = M$MIS$LLL((int32_t)tpad_$raw_y - (int32_t)config->y_min,
                            (int32_t)config->y_max_disp + 1);
            tpad_$cursor_y = TPAD_DIVS_W(acc, config->y_range);
        }

        /* 0x00E69584-0x00E695AC */
        acc = M$MIS$LLW((int32_t)tpad_$raw_x - (int32_t)config->x_min,
                        config->x_scale);
        tpad_$raw_x = (int16_t)(config->x_scale - TPAD_DIVS_W(acc, config->x_range));

        /* 0x00E695B0-0x00E695D2 */
        acc = M$MIS$LLW((int32_t)tpad_$raw_y - (int32_t)config->y_min,
                        config->y_scale);
        tpad_$raw_y = TPAD_DIVS_W(acc, config->y_range);

        goto relative_tail;
    }

    goto clamp;

relative_tail:
    /* ---------------- shared tail, 0x00E695D6-0x00E696DA ---------------- */
    {
        int8_t reorigin;   /* D5b */

        /* 0x00E695D6-0x00E695DE: `shi` is an UNSIGNED "higher than" */
        reorigin = (packet->elapsed > 0x7A12u) ? (int8_t)0xFF : (int8_t)0x00;
        reorigin = (int8_t)(reorigin | device_changed);

        /* 0x00E695E0-0x00E695FC */
        if (reorigin < 0 && config->mode != tpad_$absolute) {
            config->cursor_offset_x = (int16_t)(tpad_$cursor_x - tpad_$raw_x);
            config->cursor_offset_y = (int16_t)(tpad_$cursor_y - tpad_$raw_y);
        }

        /* 0x00E69600-0x00E6961A */
        delta_x = (int16_t)((int16_t)(tpad_$raw_x + config->cursor_offset_x) -
                            tpad_$cursor_x);
        delta_y = (int16_t)((int16_t)(tpad_$raw_y + config->cursor_offset_y) -
                            tpad_$cursor_y);

        /* 0x00E6961C cmpi.w #0x1,(-0x28,A2) */
        if (config->mode == tpad_$relative) {
            int32_t mx, my;   /* the 32-bit muls.w results */
            int16_t velocity; /* D5w */
            int16_t tfactor;  /* D3w / D5w */

            /*
             * 0x00E69624-0x00E69638.  `muls.w` sets the flags from the FULL
             * 32-bit product, but `neg.w` then negates only the low word,
             * and only the low word is used afterwards.
             */
            mx = (int32_t)delta_x * (int32_t)config->x_factor;
            v = (int16_t)mx;
            if (mx < 0) {
                v = (int16_t)(-v);
            }
            my = (int32_t)delta_y * (int32_t)config->y_factor;
            velocity = (int16_t)my;
            if (my < 0) {
                velocity = (int16_t)(-velocity);
            }
            velocity = (int16_t)(velocity + v);

            /* 0x00E6963A cmpi.w #0x64,D5w / ble -> the small-movement arm */
            if (velocity > 100) {
                /*
                 * 0x00E69640-0x00E69662.  The elapsed time is the difference
                 * of the LOW 32 BITS of the two 48-bit clocks: the packet's
                 * at packet+6 and tpad_$last_clock's at 0x00E825C2 (0x164+2).
                 */
                tfactor = TPAD_DIVS_W(
                    TPAD_CLOCK_LOW32(packet->clock_high, packet->clock_low) -
                        TPAD_CLOCK_LOW32(tpad_$last_clock.high,
                                         tpad_$last_clock.low),
                    0x2328);
                if (tfactor > 1) {
                    velocity = TPAD_DIVS_W(velocity, tfactor);
                }
                velocity = TPAD_DIVS_W(velocity, 100);
                velocity = (int16_t)(velocity + 1);
                delta_x = (int16_t)(delta_x * velocity);   /* muls.w */
                delta_y = (int16_t)(delta_y * velocity);
                accel_flag = (int8_t)0xFF;                 /* 0x00E6967C st D4b */
            }
            else if (delta_y != 0) {
                /*
                 * 0x00E69664-0x00E6967A: a nearly horizontal move snaps to
                 * horizontal.  0x00E6967A `clr.w D1w` FALLS THROUGH into
                 * 0x00E6967C `st D4b`, so this arm raises the same flag the
                 * acceleration arm does (bead source-0ni8).
                 */
                int16_t ratio = TPAD_DIVS_W(delta_x, delta_y);
                if (ratio < 0) {
                    ratio = (int16_t)(-ratio);
                }
                /* 0x00E69674 cmpi.w #0x5 / bls: UNSIGNED compare */
                if ((uint16_t)ratio > 5) {
                    delta_y = 0;                           /* 0x00E6967A */
                    accel_flag = (int8_t)0xFF;             /* 0x00E6967C */
                }
            }

            /* 0x00E6967E-0x00E69684: all six clock bytes are copied.  Every
             * arm of the relative-mode block reaches this store. */
            tpad_$last_clock.high = packet->clock_high;
            tpad_$last_clock.low = packet->clock_low;
        }

        /* 0x00E6968A-0x00E696B0: X hysteresis */
        if (delta_x > config->hysteresis) {
            tpad_$cursor_x = (int16_t)(delta_x + tpad_$cursor_x - config->hysteresis);
        }
        else if ((int16_t)(-config->hysteresis) > delta_x) {
            tpad_$cursor_x = (int16_t)(delta_x + tpad_$cursor_x + config->hysteresis);
        }

        /* 0x00E696B4-0x00E696DA: Y hysteresis */
        if (delta_y > config->hysteresis) {
            tpad_$cursor_y = (int16_t)(tpad_$cursor_y + delta_y - config->hysteresis);
        }
        else if ((int16_t)(-config->hysteresis) > delta_y) {
            tpad_$cursor_y = (int16_t)(tpad_$cursor_y + delta_y + config->hysteresis);
        }
    }

clamp:
    /* ---------------- clamp, 0x00E696DE-0x00E69738 ---------------- */
    edge_flag = 0;                                     /* 0x00E696DE clr.b D0b */
    edge_type = 0;   /* (-0x2,A6) is only written by the arms below; it is
                      * read solely on the edge_flag path, which always takes
                      * one of them.  Initialised here for the host build. */

    if (tpad_$cursor_y < config->y_min_disp) {
        edge_type = 0;                                 /* 0x00E696EA */
        tpad_$cursor_y = config->y_min_disp;
        edge_flag = (int8_t)0xFF;                      /* 0x00E69708 st D0b */
    }
    else if (tpad_$cursor_y > config->y_max_disp) {
        edge_type = 1;                                 /* 0x00E696FC */
        tpad_$cursor_y = config->y_max_disp;
        edge_flag = (int8_t)0xFF;
    }

    if (tpad_$cursor_x < config->x_min_disp) {
        edge_type = 2;                                 /* 0x00E69714 */
        tpad_$cursor_x = config->x_min_disp;
        edge_flag = (int8_t)0xFF;                      /* 0x00E69738 st D0b */
    }
    else if (tpad_$cursor_x > config->x_max_disp) {
        edge_type = 3;                                 /* 0x00E6972C */
        tpad_$cursor_x = config->x_max_disp;
        edge_flag = (int8_t)0xFF;
    }

    /* 0x00E6973A-0x00E6975A */
    if ((int8_t)(accel_flag | edge_flag) < 0 && config->mode != tpad_$absolute) {
        config->cursor_offset_x = (int16_t)(tpad_$cursor_x - tpad_$raw_x);
        config->cursor_offset_y = (int16_t)(tpad_$cursor_y - tpad_$raw_y);
    }

    /* 0x00E6975E clr.l (A1) */
    packet->elapsed = 0;

    /* 0x00E69760-0x00E6979A: the edge event */
    if (edge_flag < 0) {
        uint32_t mag_x, mag_y;   /* `clr.l D5` / `clr.l D6` zero-extend */

        v = delta_x;
        if (v < 0) {
            v = (int16_t)(-v);
        }
        mag_x = (uint32_t)(uint16_t)v;

        v = delta_y;
        if (v < 0) {
            v = (int16_t)(-v);
        }
        mag_y = (uint32_t)(uint16_t)v;

        /* 0x00E6977A-0x00E69782: the compare is a signed LONG one */
        if ((int32_t)(mag_x + mag_y) >= (int32_t)config->punch_impact) {
            (void)SMD_$LOC_EVENT((int8_t)0xFF, tpad_$unit,
                                 TPAD_CURSOR_POS(), edge_type);
        }
    }

    /* 0x00E6979E-0x00E697AE: the ordinary locator event */
    (void)SMD_$LOC_EVENT(0, tpad_$unit, TPAD_CURSOR_POS(), tpad_$button_state);
}
