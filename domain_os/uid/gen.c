/*
 * UID_$GEN - Generate a new unique identifier (0x00E1A018, 226 bytes;
 * SAU2 map: `I E1A018 UID_ size = E4`, data `D E2C008 UID_ size = C`)
 *
 * A5 = 0xE2C008 (`lea (0xe2c008).l,A5` at 0x00E1A020):
 *   A5+0x0  UID_$GENERATOR_STATE  the last UID handed out (high = clock,
 *                                 low = counter nibble in bits 28..31 over
 *                                 the 20-bit node id)
 *   A5+0x8  UID_$GENERATOR_LOCK   spin lock word
 *
 * Under the spin lock: if TIME_$CLOCKH - 0xF0 is above the stored clock it
 * becomes the new high word; otherwise the routine waits, with the lock
 * dropped, until the state's counter nibble differs from a reference nibble
 * (see the note on D2 below) or the state's high word differs from the
 * absolute clock's.  Then the state is copied out, the counter nibble is
 * bumped (carrying into the high word when it wraps), and the lock is freed.
 *
 * Frame (A6+): 0x08 uid_ret.  Locals (A6-): -0x8 the copied-out UID,
 * -0x14 TIME_$ABS_CLOCK's 6-byte result.  D2 = clock - 0xF0, later the
 * reference nibble; D3 = lock token (word, zero-extended).
 *
 * NOTE, PRESERVED AS FOUND: the reference nibble D2 is computed at
 * 0x00E1A04C from the low WORD of the abs-clock local (A6-0x10) BEFORE the
 * first TIME_$ABS_CLOCK call fills it, and is never recomputed - so on the
 * image it is derived from whatever the frame held at entry.  C cannot read
 * an uninitialised local, so the slot is zeroed here and the derivation is
 * kept; the wait loop's exit condition therefore differs from the image
 * whenever the stale word was non-zero in bits 12..15.  Bead source-ilw0.
 */

#include "uid/uid_internal.h"
#include "time/time.h"
#include "ml/ml.h"

void UID_$GEN(uid_t *uid_ret)
{
    uint32_t         clock_val;             /* D2 */
    uint32_t         ref_nibble;            /* D2 after 0x00E1A04C */
    uint32_t         state_nibble;          /* D1 */
    ml_$spin_token_t token;                 /* D3 */
    clock_t          abs_clock = { 0, 0 };  /* A6-0x14; see the note above */
    uid_t            local;                 /* A6-0x8 */
    uint8_t          counter_byte;

    /* 0x00E1A026-0x00E1A02C */
    clock_val = TIME_$CLOCKH - 0xF0u;

    /* 0x00E1A032-0x00E1A040 */
    token = ML_$SPIN_LOCK(&UID_$GENERATOR_LOCK);

    /* 0x00E1A042 `cmp.l (A5),D2 / bls`: unsigned clock_val > state.high */
    if (clock_val > UID_$GENERATOR_STATE.high) {
        UID_$GENERATOR_STATE.high = clock_val;                  /* 0x00E1A046 */
    } else {
        /* 0x00E1A04A-0x00E1A052: D2 = (word at A6-0x10) >> 12 */
        ref_nibble = (uint32_t)abs_clock.low >> 12;

        for (;;) {
            /* 0x00E1A054-0x00E1A05E */
            TIME_$ABS_CLOCK(&abs_clock);

            /* 0x00E1A060-0x00E1A066 */
            if (UID_$GENERATOR_STATE.high != abs_clock.high) {
                break;
            }
            /* 0x00E1A068-0x00E1A076: (state.low byte 0 & 0xF0) >> 4 */
            state_nibble = ((UID_$GENERATOR_STATE.low >> 24) & 0xF0u) >> 4;
            if (state_nibble != ref_nibble) {
                break;
            }

            /* 0x00E1A078-0x00E1A086: drop the lock while waiting */
            ML_$SPIN_UNLOCK(&UID_$GENERATOR_LOCK, token);

            /* 0x00E1A088-0x00E1A0A2: spin until the nibble moves */
            do {
                TIME_$ABS_CLOCK(&abs_clock);
                state_nibble = ((UID_$GENERATOR_STATE.low >> 24) & 0xF0u) >> 4;
            } while (state_nibble == ref_nibble);

            /* 0x00E1A0A4-0x00E1A0B4: retake the lock and re-check */
            token = ML_$SPIN_LOCK(&UID_$GENERATOR_LOCK);
        }
    }

    /* 0x00E1A0B6-0x00E1A0BC: copy the state out */
    local.high = UID_$GENERATOR_STATE.high;
    local.low  = UID_$GENERATOR_STATE.low;

    /* 0x00E1A0C0-0x00E1A0C2: `add.b #0x10,(0x4,A5)` - byte 0 of the low
     * longword, i.e. bits 24..31 */
    counter_byte = (uint8_t)((UID_$GENERATOR_STATE.low >> 24) + 0x10u);
    UID_$GENERATOR_STATE.low = (UID_$GENERATOR_STATE.low & 0x00FFFFFFu) |
                               ((uint32_t)counter_byte << 24);

    /* 0x00E1A0C6-0x00E1A0D2: a wrapped nibble carries into the high word */
    if (((counter_byte & 0xF0u) >> 4) == 0) {
        UID_$GENERATOR_STATE.high = UID_$GENERATOR_STATE.high + 1;
    }

    /* 0x00E1A0D4-0x00E1A0DC */
    ML_$SPIN_UNLOCK(&UID_$GENERATOR_LOCK, token);

    /* 0x00E1A0E2-0x00E1A0EC */
    uid_ret->high = local.high;
    uid_ret->low  = local.low;
}
