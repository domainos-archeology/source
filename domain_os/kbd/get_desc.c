/*
 * KBD_$GET_DESC - Keyboard descriptor for a terminal line
 *
 * Validates the line number (0..3, and below TERM_$MAX_DTTE), requires the
 * line's DTTE to carry an alternate (keyboard) handler, and for line 0
 * forces the line discipline to 1 when it is anything else.  Returns the
 * DTTE's alt_handler pointer in A0.
 *
 * Parameters (frame 0x00E1AA2E..0x00E1AA32):
 *   0x08 line_ptr   - word by reference (A2); handed on to TERM_$SET_DISCIPLINE
 *   0x0C status_ret - status return (A0)
 *
 * Original address: 0x00e1aa26, 128 bytes
 *
 *   00e1aa36  move.w (A2),D2w / cmpi.w #0x3 / bls           ; UNSIGNED <= 3
 *   00e1aa3e  move.l #0xb0007,(A0) / bra exit               ; invalid line number
 *   00e1aa46  cmp.w (0x00e2dd78).l,D2w / bcc -> not impl.   ; line >= TERM_$MAX_DTTE
 *   00e1aa4e  clr.l (A0)
 *   00e1aa50  D3 = line*8; D0 = D3*8; D3 = -D3 + D0 = line*0x38
 *   00e1aa5a  movea.l #0xe2dc90,A1                          ; DTTE
 *   00e1aa62  tst.l (0x2c,A1,D3w) / bne -> have handler
 *   00e1aa68  move.l #0xb000d,(A0) / bra exit
 *   00e1aa70  tst.w D2w / bne -> skip                       ; line 0 only
 *   00e1aa74  cmpi.w #0x1,(0x34,A1,D3w) / beq -> skip       ; discipline already 1
 *   00e1aa7c  pea (-0x4,A6) / pea (0x24,PC) / pea (A2) / jsr TERM_$SET_DISCIPLINE
 *             ; 0xE1AA82 + 0x24 = 0xE1AAA6, the WORD 0x0001; args reclaimed by unlk
 *   00e1aa8c  move.l (0x2c,A1,D3w),(-0x8,A6)
 *   00e1aa98  movea.l (-0x8,A6),A0                          ; the result
 *
 * QUIRK: on both error exits the result slot at A6-0x8 has never been
 * written, so A0 comes back holding stack residue.  Every caller in the
 * image tests the status before touching A0; the C returns NULL there.
 */

#include "kbd/kbd_internal.h"

/* 0x00E1AAA6, `gsk read 0xE1AAA6 2`: 00 01 - a word, not a byte */
static const int16_t kbd_$c_default_discipline = 0x0001;

void *KBD_$GET_DESC(uint16_t *line_ptr, status_$t *status_ret)
{
    uint16_t line;              /* D2w */
    int16_t entry_offset;       /* D3w = line * 0x38 */
    dtte_t *entry;
    status_$t local_status;     /* A6-0x4, never read */
    void *result = NULL;        /* A6-0x8; see the QUIRK note above */

    /* 0x00E1AA36..0x00E1AA3C */
    line = *line_ptr;
    if (line > 3) {
        /* 0x00E1AA3E */
        *status_ret = status_$invalid_line_number;
        return result;
    }

    /* 0x00E1AA46: unsigned compare against TERM_$MAX_DTTE */
    if (line >= (uint16_t)TERM_$MAX_DTTE) {
        /* 0x00E1AA68 */
        *status_ret = status_$requested_line_or_operation_not_implemented;
        return result;
    }

    /* 0x00E1AA4E */
    *status_ret = status_$ok;

    /* 0x00E1AA50..0x00E1AA60: line * 0x38 as a sign-extended word */
    entry_offset = (int16_t)(line * sizeof(dtte_t));
    entry = (dtte_t *)((uint8_t *)DTTE + entry_offset);

    /* 0x00E1AA62 */
    if (entry->alt_handler == 0) {
        /* 0x00E1AA68 */
        *status_ret = status_$requested_line_or_operation_not_implemented;
        return result;
    }

    /* 0x00E1AA70..0x00E1AA86 */
    if (line == 0) {
        if (entry->discipline != 1) {
            TERM_$SET_DISCIPLINE((short *)line_ptr,
                                 (void *)&kbd_$c_default_discipline,
                                 &local_status);
        }
    }

    /* 0x00E1AA8C..0x00E1AA98: the alt_handler is a 32-bit VA */
    result = ARCH_VA_TO_PTR(entry->alt_handler);
    return result;
}
