/*
 * tpad/data.c - TPAD_$DATA implementation
 *
 * Process pointing device data packets and update cursor position.
 * Handles mouse, bitpad, and touchpad devices with different packet formats.
 *
 * Original address: 0x00E691BC
 */

#include "tpad/tpad_internal.h"

/*
 * Packet byte accessor macros.
 * Packets are passed as uint32_t* but accessed as a byte array.
 */
#define PACKET_BYTE(p, n)    (((uint8_t *)(p))[n])
#define PACKET_SBYTE(p, n)   (((int8_t *)(p))[n])

/*
 * smd_$loc_event_internal - Internal locator event function
 *
 * Sends locator (mouse/trackpad) event to display manager.
 * Note: The public smd_$loc_event_internal in smd.h has a different signature
 * for user-space. This is the internal kernel interface.
 *
 * Parameters:
 *   edge_hit - 0xff if edge was hit, 0 otherwise
 *   unit - display unit number
 *   pos - cursor position (y in high word, x in low word)
 *   button_state - current button/stylus state
 *
 * Original address: 0x00E6E9A0
 */
static void smd_$loc_event_internal(uint8_t edge_hit, int16_t unit, int32_t pos, int16_t button_state)
{
    /* TODO: This should call the actual smd_$loc_event_internal implementation.
     * For now, we stub it out. The actual implementation handles:
     * - Locking the SMD request lock
     * - Queueing locator events
     * - Updating cursor position tracking
     * - Notifying waiters
     */
    (void)edge_hit;
    (void)unit;
    (void)pos;
    (void)button_state;
}

/*
 * Process mouse data packet.
 * Mouse packets are identified by byte 10 == 0xDF.
 *
 * Packet format:
 *   byte 10: 0xDF (mouse ID)
 *   byte 11: button state (bits 6-4) and overflow flags (bits 3-0)
 *   byte 12: X delta (signed)
 *   byte 13: Y delta (signed)
 */
static void process_mouse_packet(uint8_t *packet, tpad_$unit_config_t *config)
{
    int16_t button_bits;
    int16_t new_button_state;
    int8_t dx_raw, dy_raw;
    int32_t accum;
    int16_t delta_x, delta_y;

    /* Mark device as mouse */
    tpad_$dev_type = tpad_$have_mouse;

    /* Decode button state from bits 6-4 */
    button_bits = (packet[11] & 0x70) >> 4;
    /* Convert to button state: 7 - (bit0) - 2*(bit1) - (bit2)/2 */
    new_button_state = (7 - (button_bits & 1)) - ((button_bits & 2) * 2) - ((button_bits & 4) >> 1);

    /* Check if button state changed - set re-origin flag */
    if (new_button_state != tpad_$button_state) {
        tpad_$re_origin_flag = -1;
    }

    /* Process X movement if no overflow */
    if ((packet[11] & 0x03) == 0) {
        dx_raw = (int8_t)packet[12];

        /* Scale delta by x_scale factor and add to accumulator */
        accum = (int32_t)tpad_$accum_x + (int32_t)dx_raw * (int32_t)config->x_scale;
        if (accum < 0) {
            accum += 0x3ff;  /* Round toward zero */
        }
        delta_x = (int16_t)(accum >> 10);  /* Divide by 1024 */

        /* Apply hysteresis when re-origining */
        if (tpad_$re_origin_flag < 0) {
            int16_t abs_delta = delta_x < 0 ? -delta_x : delta_x;
            if (abs_delta < config->hysteresis) {
                delta_x = 0;
            }
        }

        /* Update accumulator, removing integer portion */
        tpad_$accum_x = tpad_$accum_x + dx_raw * config->x_scale - delta_x * 0x400;
    } else {
        delta_x = tpad_$delta_x;  /* Use previous delta if overflow */
    }
    tpad_$delta_x = delta_x;

    /* Apply smoothing for slow movement */
    if (config->x_scale < TPAD_$FACTOR_DEFAULT) {
        int16_t abs_delta = delta_x < 0 ? -delta_x : delta_x;
        int32_t scaled = M$MIS$LLW(abs_delta + 10, delta_x);
        delta_x = (int16_t)(scaled / 10);
    }

    /* Process Y movement if no overflow */
    if ((packet[11] & 0x0c) == 0) {
        dy_raw = (int8_t)packet[13];

        /* Scale delta by y_scale factor, subtract (Y is inverted) */
        accum = (int32_t)tpad_$accum_y - (int32_t)dy_raw * (int32_t)config->y_scale;
        if (accum < 0) {
            accum += 0x3ff;  /* Round toward zero */
        }
        delta_y = (int16_t)(accum >> 10);  /* Divide by 1024 */

        /* Apply hysteresis when re-origining */
        if (tpad_$re_origin_flag < 0) {
            int16_t abs_delta = delta_y < 0 ? -delta_y : delta_y;
            if (abs_delta < config->hysteresis) {
                delta_y = 0;
            }
        }

        /* Update accumulator, removing integer portion */
        tpad_$accum_y = tpad_$accum_y - dy_raw * config->y_scale - delta_y * 0x400;
    } else {
        delta_y = tpad_$delta_y;  /* Use previous delta if overflow */
    }
    tpad_$delta_y = delta_y;

    /* Apply smoothing for slow movement */
    if (config->y_scale < TPAD_$FACTOR_DEFAULT) {
        int16_t abs_delta = delta_y < 0 ? -delta_y : delta_y;
        int32_t scaled = M$MIS$LLW(abs_delta + 10, delta_y);
        delta_y = (int16_t)(scaled / 10);
    }

    /* Check if any change occurred */
    if (new_button_state == tpad_$button_state) {
        tpad_$re_origin_flag = 0;
        if (delta_x == 0 && delta_y == 0) {
            return;  /* No change, no event needed */
        }
    }

    /* Update cursor position and button state */
    tpad_$button_state = new_button_state;
    tpad_$cursor_x += delta_x;
    tpad_$cursor_y += delta_y;
}

/*
 * Process bitpad data packet.
 * Bitpad packets are identified by byte 10 == 0x01.
 *
 * Packet format:
 *   byte 10: 0x01 (bitpad ID)
 *   byte 11: button state (bits 5-2)
 *   byte 12: X low 6 bits
 *   byte 13: X high 6 bits (multiply by 64)
 *   byte 14: Y low 6 bits
 *   byte 15: Y high 6 bits (multiply by 64)
 */
static int process_bitpad_packet(uint8_t *packet, tpad_$unit_config_t *config)
{
    int16_t raw_x, raw_y;
    int device_changed;

    /* Check if device type changed */
    device_changed = (tpad_$dev_type != tpad_$have_bitpad);
    tpad_$dev_type = tpad_$have_bitpad;

    /* Decode X coordinate: (byte13 << 6) + byte12 */
    raw_x = ((int16_t)packet[13] << 6) + packet[12];
    /* Scale to display coordinates */
    tpad_$raw_x = (raw_x * config->x_scale) / TPAD_$BITPAD_SCALE;

    /* Decode Y coordinate: (byte15 << 6) + byte14, then invert */
    raw_y = ((int16_t)packet[15] << 6) + packet[14];
    /* Scale and invert (Y increases downward on display) */
    tpad_$raw_y = config->y_scale - (raw_y * config->y_scale) / TPAD_$BITPAD_SCALE;

    /* Decode button state from bits 5-2 */
    tpad_$button_state = (packet[11] & 0x3c) >> 2;

    return device_changed;
}

/*
 * Process touchpad data packet.
 * Touchpad packets have any byte 10 value other than 0xDF or 0x01.
 *
 * Packet format:
 *   byte 11: X low 8 bits
 *   byte 12: X high nibble (bits 0-3), Y low nibble (bits 4-7)
 *   byte 13: Y high 8 bits
 */
static int process_touchpad_packet(uint8_t *packet, tpad_$unit_config_t *config)
{
    int16_t raw_x, raw_y;
    int device_changed;

    /* Check if device type changed */
    device_changed = (tpad_$dev_type != tpad_$have_touchpad);
    tpad_$dev_type = tpad_$have_touchpad;
    tpad_$button_state = 0;

    /* Decode X coordinate: (byte12 & 0x0f) << 8 + byte11 */
    raw_x = ((packet[12] & 0x0f) << 8) + packet[11];

    /* Decode Y coordinate: byte13 << 4 + (byte12 >> 4) */
    raw_y = ((int16_t)packet[13] << 4) + ((packet[12] & 0xf0) >> 4);

    /* Handle inverted touchpad orientation */
    if (tpad_$touchpad_max >= TPAD_$TOUCHPAD_INVERTED) {  /* 0x1000 */
        raw_x = 0xfff - raw_x;
        raw_y = 0xfff - raw_y;
    }

    /* Check if coordinates are within valid range */
    if (raw_x > tpad_$touchpad_max || raw_y > tpad_$touchpad_max) {
        /* Out of range - return without updating position */
        return 0;
    }

    tpad_$raw_x = raw_x;
    tpad_$raw_y = raw_y;

    /* Increment sample count for auto-ranging */
    config->sample_count++;

    /* Auto-ranging: track min/max coordinates over first 1000 samples */
    if (config->sample_count < TPAD_$RANGING_SAMPLES) {
        /* Track X minimum */
        if (raw_x + TPAD_$RANGING_MARGIN < config->x_min) {
            config->x_min = raw_x + TPAD_$RANGING_MARGIN;
        }
        /* Track X range */
        if (raw_x - TPAD_$RANGING_MARGIN - config->x_min > config->x_range) {
            config->x_range = raw_x - TPAD_$RANGING_MARGIN - config->x_min;
            /* Recompute X factor */
            if (config->x_scale == 0) {
                config->x_factor = TPAD_$FACTOR_DEFAULT;
            } else {
                config->x_factor = config->x_range / config->x_scale;
            }
        }

        /* Track Y minimum */
        if (raw_y + TPAD_$RANGING_MARGIN < config->y_min) {
            config->y_min = raw_y + TPAD_$RANGING_MARGIN;
        }
        /* Track Y range */
        if (raw_y - TPAD_$RANGING_MARGIN - config->y_min > config->y_range) {
            config->y_range = raw_y - TPAD_$RANGING_MARGIN - config->y_min;
            /* Recompute Y factor */
            if (config->y_scale == 0) {
                config->y_factor = TPAD_$FACTOR_DEFAULT;
            } else {
                config->y_factor = config->y_range / config->y_scale;
            }
        }
    }

    /* Convert raw coordinates to display coordinates */
    {
        int32_t scaled;
        uint32_t *pkt = (uint32_t *)packet;

        /* In scaled mode with sufficient time elapsed, use absolute positioning */
        if (pkt[0] > 125000 && config->mode == tpad_$scaled) {
            /* X: map from raw range to display range */
            scaled = M$MIS$LLW(raw_x - config->x_min, config->x_max_disp);
            tpad_$cursor_x = (config->x_max_disp + 1) - (int16_t)(scaled / config->x_range);

            /* Y: map from raw range to display range */
            scaled = M$MIS$LLL(raw_y - config->y_min, config->y_max_disp + 1);
            tpad_$cursor_y = (int16_t)(scaled / config->y_range);
        }

        /* Convert raw to scaled coordinates */
        scaled = M$MIS$LLW(raw_x - config->x_min, config->x_scale);
        tpad_$raw_x = config->x_scale - (int16_t)(scaled / config->x_range);

        scaled = M$MIS$LLW(raw_y - config->y_min, config->y_scale);
        tpad_$raw_y = (int16_t)(scaled / config->y_range);
    }

    return device_changed;
}

/*
 * Apply relative mode processing for bitpad/touchpad.
 * Updates cursor position based on device movement with acceleration.
 */
static int apply_relative_mode(tpad_$unit_config_t *config, int device_changed,
                                uint32_t *packet)
{
    int16_t delta_x, delta_y;
    int edge_hit = 0;

    /* If device changed or enough time elapsed, reset cursor offset tracking */
    if (device_changed || packet[0] > 31250) {  /* ~31ms threshold */
        if (config->mode != tpad_$absolute) {
            config->cursor_offset_x = tpad_$cursor_x - tpad_$raw_x;
            config->cursor_offset_y = tpad_$cursor_y - tpad_$raw_y;
        }
    }

    /* Calculate cursor delta from device position change */
    delta_x = (tpad_$raw_x + config->cursor_offset_x) - tpad_$cursor_x;
    delta_y = (tpad_$raw_y + config->cursor_offset_y) - tpad_$cursor_y;

    /* In relative mode, apply acceleration */
    if (config->mode == tpad_$relative) {
        int16_t velocity;
        int32_t abs_dx = delta_x < 0 ? -delta_x : delta_x;
        int32_t abs_dy = delta_y < 0 ? -delta_y : delta_y;

        /* Scale velocity by conversion factors */
        abs_dx = (int32_t)(int16_t)abs_dx * config->x_factor;
        if (abs_dx < 0) abs_dx = -abs_dx;
        abs_dy = (int32_t)(int16_t)abs_dy * config->y_factor;
        if (abs_dy < 0) abs_dy = -abs_dy;

        velocity = (int16_t)(abs_dx + abs_dy);

        /* Only apply acceleration if velocity exceeds threshold */
        if (velocity < 100) {
            /* Small movement - check for nearly horizontal/vertical */
            if (delta_y != 0) {
                int16_t ratio = delta_x / delta_y;
                if (ratio < 0) ratio = -ratio;
                if (ratio > 5) {
                    delta_y = 0;  /* Snap to horizontal */
                }
            }
        } else {
            /* Apply acceleration based on velocity */
            int16_t time_factor;
            /*
             * For time comparison, we use the low 32 bits of the 48-bit clock.
             * The original code accesses bytes at offset 0x166 from base, which
             * overlaps the middle portion of the clock_t struct - essentially
             * using (high.low16 << 16) | low as a 32-bit approximation.
             *
             * We simplify here by using just the high 32 bits for comparison.
             */
            int32_t last_time = (int32_t)tpad_$last_clock.high;
            int32_t time_diff = (int32_t)packet[1] - last_time;
            time_factor = (int16_t)(time_diff / 9000);  /* Convert to time units */
            if (time_factor > 1) {
                velocity = velocity / time_factor;
            }
            time_factor = velocity / 100 + 1;
            delta_x *= time_factor;
            delta_y *= time_factor;
            edge_hit = 1;
        }

        /* Save timestamp for next acceleration calculation */
        tpad_$last_clock.high = packet[1];
        tpad_$last_clock.low = 0;
    }

    /* Apply delta to cursor, respecting hysteresis */
    if (delta_x > config->hysteresis) {
        tpad_$cursor_x = tpad_$cursor_x + delta_x - config->hysteresis;
    } else if (delta_x < -config->hysteresis) {
        tpad_$cursor_x = tpad_$cursor_x + delta_x + config->hysteresis;
    }

    if (delta_y > config->hysteresis) {
        tpad_$cursor_y = tpad_$cursor_y + delta_y - config->hysteresis;
    } else if (delta_y < -config->hysteresis) {
        tpad_$cursor_y = tpad_$cursor_y + delta_y + config->hysteresis;
    }

    return edge_hit;
}

/*
 * Clamp cursor to display boundaries and check for edge hits.
 */
static int clamp_cursor(tpad_$unit_config_t *config, int16_t *edge_type)
{
    int edge_hit = 0;

    /* Clamp Y to display boundaries */
    if (tpad_$cursor_y < config->y_min_disp) {
        *edge_type = 0;
        tpad_$cursor_y = config->y_min_disp;
        edge_hit = 1;
    } else if (tpad_$cursor_y > config->y_max_disp) {
        *edge_type = 1;
        tpad_$cursor_y = config->y_max_disp;
        edge_hit = 1;
    }

    /* Clamp X to display boundaries */
    if (tpad_$cursor_x < config->x_min_disp) {
        *edge_type = 2;
        tpad_$cursor_x = config->x_min_disp;
        edge_hit = 1;
    } else if (tpad_$cursor_x > config->x_max_disp) {
        *edge_type = 3;
        tpad_$cursor_x = config->x_max_disp;
        edge_hit = 1;
    }

    return edge_hit;
}

/*
 * TPAD_$DATA - Process pointing device data packet
 *
 * Called by the keyboard/input driver when a pointing device packet
 * is received. Processes the raw data and updates cursor position.
 */
void TPAD_$DATA(uint32_t *packet)
{
    tpad_$unit_config_t *config;
    uint8_t *pkt = (uint8_t *)packet;
    uint8_t device_id;
    int device_changed = 0;
    int edge_hit_accel = 0;
    int edge_hit_clamp = 0;
    int16_t edge_type = 0;
    int16_t delta_x, delta_y;
    int32_t cursor_pos;

    /* Get current unit configuration */
    config = TPAD_$UNIT_CONFIG(tpad_$unit);

    /* Identify device type from packet byte 10 */
    device_id = pkt[10];

    if (device_id == TPAD_$MOUSE_ID) {  /* 0xDF = mouse */
        /* Process mouse packet */
        process_mouse_packet(pkt, config);
    } else {
        if (device_id == TPAD_$BITPAD_ID) {  /* 0x01 = bitpad */
            device_changed = process_bitpad_packet(pkt, config);
        } else {
            /* Touchpad */
            device_changed = process_touchpad_packet(pkt, config);
            if (tpad_$dev_type == tpad_$have_touchpad && tpad_$button_state == 0) {
                return;  /* No touch detected */
            }
        }

        /* Apply relative mode processing */
        edge_hit_accel = apply_relative_mode(config, device_changed, packet);
    }

    /* Clamp cursor to display boundaries */
    edge_hit_clamp = clamp_cursor(config, &edge_type);

    /* Update cursor offset if edge was hit or acceleration applied */
    if ((edge_hit_accel || edge_hit_clamp) && config->mode != tpad_$absolute) {
        config->cursor_offset_x = tpad_$cursor_x - tpad_$raw_x;
        config->cursor_offset_y = tpad_$cursor_y - tpad_$raw_y;
    }

    /* Clear timestamp in packet to mark as processed */
    packet[0] = 0;

    /* Calculate cursor position as 32-bit value (y in high word, x in low word) */
    cursor_pos = ((int32_t)tpad_$cursor_y << 16) | (tpad_$cursor_x & 0xffff);

    /* Send edge event if edge was hit with sufficient velocity */
    if (edge_hit_clamp) {
        delta_x = tpad_$delta_x < 0 ? -tpad_$delta_x : tpad_$delta_x;
        delta_y = tpad_$delta_y < 0 ? -tpad_$delta_y : tpad_$delta_y;
        if (delta_x + delta_y >= config->punch_impact) {
            smd_$loc_event_internal(0xff, tpad_$unit, cursor_pos, edge_type);
        }
    }

    /* Send normal locator event */
    smd_$loc_event_internal(0, tpad_$unit, cursor_pos, tpad_$button_state);
}
