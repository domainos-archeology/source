/*
 * PROC1_$LOADAV_CALLBACK - Recompute the three load averages
 * Original address: 0x00e14bda (186 bytes)
 *
 * Re-emitted from the disassembly.  A5 = 0x00E254E8; (0,A5)/(4,A5)/(8,A5)
 * are PROC1_$LOADAV[0..2].  Fired every 5 s from TIME_$RTEQ through the
 * repeating element PROC1_$INIT_LOADAV enters (the only reference to this
 * routine is the `move.l #0xe14bda' at 0x00E14CB0).
 *
 * Each average is an 8.24 fixed-point exponential moving average:
 *
 *   avg = ((avg div 256) * decay) div 256 + ready * gain
 *
 * where `div 256' is the Pascal signed division (`bpl / addi.l #0xff /
 * asr.l #8': bias negatives by 255 so the shift rounds toward zero),
 * the multiply is M$MIS$LLL (0x00E0ABD4, signed 32x32) and `ready * gain'
 * is `muls.w' on the ready count (a signed 16x16 -> 32 product).
 *
 * 0x00E14BE8  D1 = PROC1_$READY_COUNT (0xE1EBD0)
 * 0x00E14BEE  D0 = LOADAV[0] div 256; D0 = M$MIS$LLL(D0, 0xEB88) div 256
 * 0x00E14C14  D2 = D1 * 0x1478; LOADAV[0] = D0 + D2
 * 0x00E14C20  D2 = LOADAV[1] div 256; D0 = M$MIS$LLL(D2, 0xFBC5) div 256
 * 0x00E14C48  D2 = D1 * 0x043B; LOADAV[1] = D0 + D2
 * 0x00E14C56  D1 = D1 * 0x016B                    (computed first here)
 * 0x00E14C5A  D0 = LOADAV[2] div 256; D0 = M$MIS$LLL(D0, 0xFE95) div 256
 * 0x00E14C84  LOADAV[2] = D1 + D0
 *
 * Decay constants are 0.16 fractions of e^(-5/60), e^(-5/300), e^(-5/900);
 * the gains are (1 - decay) scaled by 2^24 / 256.
 */

#include "proc1/proc1_internal.h"
#include "math/math.h"

/* 0x00E14BFA / 0x00E14C2E / 0x00E14C68 */
#define PROC1_LOADAV_DECAY_1MIN    0xEB88L
#define PROC1_LOADAV_DECAY_5MIN    0xFBC5L
#define PROC1_LOADAV_DECAY_15MIN   0xFE95L

/* 0x00E14C18 / 0x00E14C4C / 0x00E14C56 */
#define PROC1_LOADAV_GAIN_1MIN     0x1478
#define PROC1_LOADAV_GAIN_5MIN     0x043B
#define PROC1_LOADAV_GAIN_15MIN    0x016B

/* `bpl / addi.l #0xff / asr.l #8': Pascal `div 256' on a signed longword */
static int32_t proc1_$loadav_div256(int32_t v)
{
    if (v < 0) {
        v += 0xFF;
    }
    return v >> 8;
}

void PROC1_$LOADAV_CALLBACK(void)
{
    int16_t ready;              /* D1.w */
    int32_t d0;
    int32_t d2;

    /* 0x00E14BE8 */
    ready = (int16_t)PROC1_$READY_COUNT;

    /* 0x00E14BEE..0x00E14C1E */
    d0 = proc1_$loadav_div256(PROC1_$LOADAV[0]);
    d0 = (int32_t)M$MIS$LLL(d0, PROC1_LOADAV_DECAY_1MIN);
    d0 = proc1_$loadav_div256(d0);
    d2 = (int32_t)ready * PROC1_LOADAV_GAIN_1MIN;
    PROC1_$LOADAV[0] = d0 + d2;

    /* 0x00E14C20..0x00E14C52 */
    d2 = proc1_$loadav_div256(PROC1_$LOADAV[1]);
    d0 = (int32_t)M$MIS$LLL(d2, PROC1_LOADAV_DECAY_5MIN);
    d0 = proc1_$loadav_div256(d0);
    d2 = (int32_t)ready * PROC1_LOADAV_GAIN_5MIN;
    PROC1_$LOADAV[1] = d0 + d2;

    /* 0x00E14C56..0x00E14C86 */
    d2 = (int32_t)ready * PROC1_LOADAV_GAIN_15MIN;
    d0 = proc1_$loadav_div256(PROC1_$LOADAV[2]);
    d0 = (int32_t)M$MIS$LLL(d0, PROC1_LOADAV_DECAY_15MIN);
    d0 = proc1_$loadav_div256(d0);
    PROC1_$LOADAV[2] = d2 + d0;
}
