/*
 * SIO_$I_GET_DESC - SIO descriptor for a terminal line
 *
 * Maps the caller's line through TERM_$GET_REAL_LINE and returns the
 * tty_handler cell of that line's DTTE, or sets
 * status_$requested_line_or_operation_not_implemented when the cell is
 * zero.
 *
 * Original address: 0x00E667C6, 86 bytes (SAU2 map: OS_TERM segment at
 * 0xE66738).  0xE2DC90 is TERM_$DATA.dtte (TERM_$DATA 0xE2C9F0 + 0x12A0),
 * indexed by 0x38-byte entries; (0x28,A0,D0w) is dtte_t.tty_handler.
 *
 *   00e667c6    link.w A6,-0x8
 *   00e667ca    movem.l {A2 D2},-(SP)
 *   00e667ce    movea.l (0xa,A6),A2            ; status_ret
 *   00e667d2    subq.l #0x2,SP                 ; word result slot
 *   00e667d4    pea (A2)
 *   00e667d6    move.w (0x8,A6),-(SP)          ; line_num (word)
 *   00e667da    jsr 0x00e1a9d4.l               ; TERM_$GET_REAL_LINE
 *   00e667e0    addq.w #0x8,SP
 *   00e667e2    move.w D0w,D1w                 ; real line
 *   00e667e4    tst.l (A2)
 *   00e667e6    bne.b 0x00e6680e               ; status set -> return (-0x4,A6)
 *   00e667e8    move.w D1w,D0w
 *   00e667ea    lsl.w #0x3,D0w                 ; line*8
 *   00e667ec    move.w D0w,D2w
 *   00e667ee    neg.w D0w
 *   00e667f0    lsl.w #0x3,D2w                 ; line*64
 *   00e667f2    movea.l #0xe2dc90,A0           ; DTTE
 *   00e667f8    add.w D2w,D0w                  ; line*0x38
 *   00e667fa    tst.l (0x28,A0,D0w*0x1)        ; DTTE[line].tty_handler
 *   00e667fe    bne.b 0x00e66808
 *   00e66800    move.l #0xb000d,(A2)           ; not implemented
 *   00e66806    bra.b 0x00e6680e
 *   00e66808    move.l (0x28,A0,D0w*0x1),(-0x4,A6)
 *   00e6680e    movea.l (-0x4,A6),A0           ; result
 *   00e66812    movem.l (-0x10,A6),{D2 A2}
 *   00e66818    unlk A6
 *   00e6681a    rts
 *
 * Note (0x00E6680E): the result slot (-0x4,A6) is only written on the
 * success path, so both failure paths return whatever the slot held.
 * The C returns NULL on those paths.
 */

#include "sio/sio_internal.h"

sio_desc_t *SIO_$I_GET_DESC(int16_t line_num, status_$t *status_ret)
{
    int16_t real_line;
    sio_desc_t *result = NULL;      /* (-0x4,A6): uninitialised in the image */

    /* 0x00E667D2-0x00E667E2 */
    real_line = TERM_$GET_REAL_LINE(line_num, status_ret);

    /* 0x00E667E4-0x00E667E6 */
    if (*status_ret == status_$ok) {
        /* 0x00E667E8-0x00E66808 */
        if (DTTE[real_line].tty_handler == 0) {
            *status_ret = status_$requested_line_or_operation_not_implemented;
        } else {
            result = (sio_desc_t *)ARCH_VA_TO_PTR(DTTE[real_line].tty_handler);
        }
    }
    return result;
}
