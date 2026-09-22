/*
 * KBD_$RCV - Keyboard receive handler
 *
 * Installed in TERM_$DATA.ptr_kbd_rcv (+0x88) and called by the SIO receive
 * path with (descriptor, byte).  The byte is run through the keyboard state
 * machine: kbd_$state_lookup finds the table entry for (state, byte), whose
 * high nibble selects an action and whose low nibble is the next state
 * (0xF = the keyboard type's escape state from DAT_00e2ddec).
 *
 * Actions (jump table at 0x00E1CCFE, 13 word offsets, `gsk read 0xE1CCFE 26`):
 *   0, 7, 8, 9      0x0120 -> 0x00E1CE1E   nothing, just the state update
 *   1, 10, 11, 12   0x003C -> 0x00E1CD3A   kbd_$process_key
 *   2               0x001A -> 0x00E1CD18   manual stop (crash) unless MMU normal mode
 *   3               0x004E -> 0x00E1CD4C   touchpad byte 0, reset the touchpad cursor
 *   4               0x0060 -> 0x00E1CD5E   touchpad byte 1
 *   5               0x006C -> 0x00E1CD6A   touchpad byte 2
 *   6               0x007A -> 0x00E1CD78   touchpad byte 3, then queue the sample
 *   >= 13           bcc.w  -> 0x00E1CE1E
 *
 * Parameters (frame 0x00E1CCCE):
 *   0x08 state - the descriptor (A2)
 *   0x0C key   - the received byte, in the HIGH byte of its word slot
 *                (`move.b (0xc,A6),...` throughout)
 *
 * Original address: 0x00e1ccc0, 432 bytes
 *
 * A5 = 0xE2DDE4 (KBD data segment): (0x8,A5) DAT_00e2ddec, (0x58,A5) /
 * (0x5a,A5) / (0x5c,A5) = TERM_$TPAD_BUFFER.head / .tail / .samples
 * (0xE2DE3C).
 *
 * Frame: -0x1E fetch result (byte), -0x1C data cell for DXM, -0x18 fetched
 * key, -0x16 next head index, -0x14 fetched mode, -0x10 clock sample,
 * -0x08 DXM status.
 *
 *   00e1ccd2  move.b key / move.w (0x38,A2) / bsr kbd_$state_lookup -> A3
 *   00e1cce2  D0 = (entry->next & 0xF0) >> 4; cmpi.w #0xd / bcc; jump table
 *   -- action 2 (0x00E1CD18) --
 *   00e1cd18  jsr MMU_$NORMAL_MODE / tst.b D0b / bmi -> action 1
 *   00e1cd22  pea (0x168,PC) -> 0xE1CE8C (Term_Manual_Stop_err) / jsr CRASH_SYSTEM
 *   00e1cd2e  bsr KBD_$CRASH_INIT
 *   00e1cd32  moveq #0xf,D0 / or.w D0w,(A3)          ; entry->next |= 0x0F
 *   00e1cd36  bra.w 0x00e1ce1e                       ; NOT into process_key
 *   -- action 1 (0x00E1CD3A) --
 *   00e1cd3a  subq.l #2 / pea (A2) / move.b key / bsr kbd_$process_key / addq #8
 *   -- action 3..6 --
 *   00e1cd4c  move.b key,(0x26,A2); (0x2c,A2) = A2 + 0x27
 *   00e1cd5e  ((0x2c,A2))[0] = key
 *   00e1cd6a  ((0x2c,A2))[1] = key
 *   00e1cd78  ((0x2c,A2))[2] = key; tst.l (A2) / bne -> state update
 *   00e1cd88  D0 = head + 1; (-0x16) = D0; if D0 == 6 -> 0
 *   00e1cd9c  TIME_$CLOCK(&(-0x10))
 *   00e1cda8  D0 = ext.l tail; D1 = new head (zero-extended); cmp.l / beq -> 0x00E1CE18
 *   00e1cdb8  (0x20,A2) = clock.high; (0x24,A2) = clock.low
 *   00e1cdc4  (0x1c,A2) = long at (0x22,A2) - (0x18,A2)   ; low 32 clock bits - last_time
 *   00e1cdd0  16 bytes from (0x1c,A2) to samples[head] ((0x5c,A5) + head*16)
 *   00e1cde8  head = (-0x16)
 *   00e1cdee  DXM_$ADD_CALLBACK(&DXM_$UNWIRED_Q, 0xE1CE90 cell, &(&state->tpad_buffer),
 *                               4, true, &(-0x8))     ; pea (0x8a,PC) = 0xE1CE90
 *   00e1ce18  (0x18,A2) = long at (-0xe,A6)           ; last_time = low 32 clock bits
 *   -- state update (0x00E1CE1E) --
 *   00e1ce1e  D0 = entry->next & 0xF; == 0xF -> state = DAT_00e2ddec[kbd_type_idx]
 *             else state = D0
 *   -- handler drain (0x00E1CE3C) --
 *   00e1ce3c  tst.l (A2) / beq -> exit
 *   00e1ce62  loop: kbd_$fetch_key(A2, &key, &mode) -> (-0x1e); D0w = mode
 *   00e1ce7c  tst.b (-0x1e) / bmi -> 0x00E1CE44 else exit
 *   00e1ce44  tst.w D0w / bne -> loop                 ; only mode 0 keys are delivered
 *   00e1ce48  subq.l #2 (handler result) / subq.l #2 (translate result) /
 *             move.b key / bsr kbd_$translate_key / addq #4 /
 *             move.b D0b,-(SP) / move.l (0x14,A2),-(SP) / jsr (handler) / addq #8
 */

#include "kbd/kbd_internal.h"

void KBD_$RCV(kbd_state_t *state, uint8_t key)
{
    kbd_$state_entry_t *entry;      /* A3 */
    uint16_t action;                /* D0w at 0x00E1CCE2 */
    uint16_t next;                  /* D0w at 0x00E1CE1E */
    uint16_t new_head;              /* A6-0x16 */
    clock_t now;                    /* A6-0x10 */
    status_$t dxm_status;           /* A6-0x8 */
    void *data_cell;                /* A6-0x1C */
    uint8_t fetched_key;            /* A6-0x18 */
    int16_t fetched_mode;           /* A6-0x14 */
    int8_t fetched;                 /* A6-0x1E */
    suma_sample_t *sample;
    uint32_t clock_low32;

    /* 0x00E1CCD2..0x00E1CCE0 */
    entry = (kbd_$state_entry_t *)kbd_$state_lookup(state->state, key);

    /* 0x00E1CCE2..0x00E1CCFA */
    action = (uint16_t)((entry->next & 0xF0) >> 4);

    switch (action) {
    case 2:
        /* 0x00E1CD18..0x00E1CD20: a Domain boolean, true is negative */
        if (MMU_$NORMAL_MODE() < 0) {
            /* 0x00E1CD3A: the same call as action 1 */
            kbd_$process_key(key, state);
            break;
        }
        /* 0x00E1CD22..0x00E1CD34 */
        CRASH_SYSTEM(&Term_Manual_Stop_err);
        KBD_$CRASH_INIT();
        entry->next = (uint8_t)(entry->next | 0x0F);
        /* 0x00E1CD36: bra.w 0x00e1ce1e */
        break;

    case 1:
    case 10:
    case 11:
    case 12:
        /* 0x00E1CD3A..0x00E1CD48 */
        kbd_$process_key(key, state);
        break;

    case 3:
        /* 0x00E1CD4C..0x00E1CD5A */
        state->tpad_x = key;
        state->tpad_ptr = &state->tpad_y;
        break;

    case 4:
        /* 0x00E1CD5E..0x00E1CD66 */
        state->tpad_ptr[0] = key;
        break;

    case 5:
        /* 0x00E1CD6A..0x00E1CD74 */
        state->tpad_ptr[1] = key;
        break;

    case 6:
        /* 0x00E1CD78..0x00E1CD7C */
        state->tpad_ptr[2] = key;

        /* 0x00E1CD82: with a handler installed the sample is not queued */
        if (state->handler != NULL) {
            break;
        }

        /* 0x00E1CD88..0x00E1CD98 */
        new_head = (uint16_t)(TERM_$TPAD_BUFFER.head + 1);
        if (new_head == SUMA_TPAD_BUFFER_SIZE) {
            new_head = 0;
        }

        /* 0x00E1CD9C..0x00E1CDA6 */
        TIME_$CLOCK(&now);
        clock_low32 = ((uint32_t)now.high << 16) | now.low;

        /* 0x00E1CDA8..0x00E1CDB6: tail sign-extended, new head zero-extended */
        if ((int32_t)(int16_t)TERM_$TPAD_BUFFER.tail != (int32_t)new_head) {
            /* 0x00E1CDB8..0x00E1CDBE */
            state->clock_high = now.high;
            state->clock_low = now.low;

            /* 0x00E1CDC4..0x00E1CDCC: the longword at +0x22 is the low 32
             * bits of the 48-bit clock just stored */
            state->delta_time =
                (((uint32_t)state->clock_high << 16) | state->clock_low) -
                state->last_time;

            /*
             * 0x00E1CDD0..0x00E1CDE6: 16 bytes from state+0x1C into
             * samples[head]; field for field, since the descriptor's
             * 0x1C..0x2B and suma_sample_t have the same layout.
             */
            sample = &TERM_$TPAD_BUFFER.samples[(int16_t)TERM_$TPAD_BUFFER.head];
            sample->delta_time = state->delta_time;
            sample->timestamp_high = state->clock_high;
            sample->timestamp_low = state->clock_low;
            sample->id_flags = state->tpad_x;
            sample->reserved_0b = state->tpad_y;
            sample->x_high = state->tpad_z;
            sample->x_low = state->pad_29[0];
            sample->y_high = state->pad_29[1];
            sample->y_low = state->pad_29[2];

            /* 0x00E1CDE8 */
            TERM_$TPAD_BUFFER.head = new_head;

            /* 0x00E1CDEE..0x00E1CE14: data = the 4-byte cell holding
             * &state->tpad_buffer; check_dup = true */
            data_cell = &state->tpad_buffer;
            DXM_$ADD_CALLBACK(&DXM_$UNWIRED_Q, &PTR_TERM_$ENQUEUE_TPAD_00e1ce90,
                              &data_cell, 4, true, &dxm_status);
        }

        /* 0x00E1CE18: reached on the full-buffer path too */
        state->last_time = clock_low32;
        break;

    default:
        /* 0, 7, 8, 9 and >= 13: 0x00E1CE1E straight away */
        break;
    }

    /* 0x00E1CE1E..0x00E1CE38 */
    next = (uint16_t)(entry->next & 0x0F);
    if (next == 0x0F) {
        state->state = DAT_00e2ddec[state->kbd_type_idx];
    } else {
        state->state = next;
    }

    /* 0x00E1CE3C..0x00E1CE80 */
    if (state->handler != NULL) {
        for (;;) {
            fetched = kbd_$fetch_key(state, &fetched_key, &fetched_mode);
            if (fetched >= 0) {
                break;
            }
            if (fetched_mode != 0) {
                continue;
            }
            /* 0x00E1CE48..0x00E1CE60 */
            ((kbd_$handler_fn_t)state->handler)(state->user_data,
                                               kbd_$translate_key(fetched_key));
        }
    }
}
