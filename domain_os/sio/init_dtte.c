/*
 * SIO_$INIT_DTTE - Initialise a DTTE
 *
 * EC_$INITs the entry's three eventcounts (+0x0C, +0x18, +0x00 in that
 * order), clears the flags byte and stores the discipline word.  Called by
 * SIO_$INIT (0x00E32D86 with 2, 0x00E32E56 with 0).
 *
 * Original address: 0x00E32B76, 66 bytes (SAU2 map: OS_TERM segment)
 *
 *   00e32b76    link.w A6,-0x4
 *   00e32b7a    movem.l {A2 D2},-(SP)
 *   00e32b7e    movea.l (0x8,A6),A2            ; dtte
 *   00e32b82    move.w (0xc,A6),D2w            ; discipline
 *   00e32b86    pea (0xc,A2)
 *   00e32b8a    jsr 0x00e151fe.l               ; EC_$INIT(&dtte->input_ec)
 *   00e32b90    addq.w #0x4,SP
 *   00e32b92    pea (0x18,A2)
 *   00e32b96    jsr 0x00e151fe.l               ; EC_$INIT(&dtte->output_ec)
 *   00e32b9c    addq.w #0x4,SP
 *   00e32b9e    pea (A2)
 *   00e32ba0    jsr 0x00e151fe.l               ; EC_$INIT(dtte + 0)
 *   00e32ba6    clr.b (0x36,A2)                ; flags = 0
 *   00e32baa    move.w D2w,(0x34,A2)           ; discipline
 *   00e32bae    movem.l (-0xc,A6),{D2 A2}
 *   00e32bb4    unlk A6
 *   00e32bb6    rts
 */

#include "sio/sio_internal.h"

void SIO_$INIT_DTTE(dtte_t *dtte, int16_t discipline)
{
    /* 0x00E32B86-0x00E32BA0 */
    EC_$INIT((ec_$eventcount_t *)(void *)((uint8_t *)dtte + 0x0C));
    EC_$INIT((ec_$eventcount_t *)(void *)((uint8_t *)dtte + 0x18));
    EC_$INIT((ec_$eventcount_t *)(void *)dtte);

    /* 0x00E32BA6-0x00E32BAA */
    dtte->flags = 0;
    dtte->discipline = discipline;
}
