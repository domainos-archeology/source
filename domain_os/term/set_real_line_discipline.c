/*
 * TERM_$SET_REAL_LINE_DISCIPLINE - Set a real line's discipline
 *
 * Disciplines 0 and 3 re-point the line's SIO descriptor (the sio_desc_t
 * SIO_$INIT_DESC stored into dtte->tty_handler): 0 restores the four TTY
 * handlers from TERM_$DATA +0x18..+0x24 and the owner from
 * dtte->handler_ptr, 3 installs SUMA_$RCV as the receive handler.
 * Disciplines 1 and 2 write the keyboard descriptor's handler cell (the
 * longword dtte->alt_handler points at): 1 clears it, 2 installs
 * TERM_$DATA.ptr_tty_i_rcv_alt, reloads the font and replays the keyboard
 * string.  Any other discipline is just recorded.  All descriptor writes
 * are made under TTY_$SPIN_LOCK.
 *
 * Parameters (frame 0x00E1AB70..0x00E1AB94):
 *   0x08 line_ptr       - word by reference (A0)
 *   0x0C discipline_ptr - word by reference (A2)
 *   0x10 status_ret     - status return (A1)
 *
 * Original address: 0x00e1ab62, 306 bytes
 *
 * A5 = 0xE2C9F0 (TERM_$DATA); D3 = line, D2 = discipline.
 *
 *   00e1ab78  cmpi.w #3,(A0) / bhi -> 0xE1AC24 (0xb0007)         ; UNSIGNED
 *   00e1ab80  cmp.w (0x1388,A5),D0w / bcs; 0xb000d / bra exit     ; TERM_$MAX_DTTE
 *   00e1ab92  clr.l (A1)
 *   00e1ab9e  cmpi.w #4,D0w / bcc.w -> 0xE1AC7E (store only)     ; UNSIGNED
 *   00e1aba6  jump table at 0xE1ABB0: 0008 005E 005E 0008
 *             -> discipline 0, 3 : 0xE1ABB8; 1, 2 : 0xE1AC0E
 *   -- 0 / 3 --
 *   00e1abb8  A2 = A5 + line*0x38; D4 = (0x12c8,A2) tty_handler; beq -> 0xE1AC24
 *   00e1abce  pea (0x1384,A5) / jsr ML_$SPIN_LOCK / addq #4; token -> (-0xe,A6)
 *   00e1abe0  tst.w D2w / bne -> 3
 *   00e1abe4  (0x4,A0) = (0x12c4,A2) handler_ptr; (0x28..0x34,A0) = (0x18..0x24,A5)
 *   00e1ac04  3: (0x28,A0) = 0xE1AD18 SUMA_$RCV
 *   00e1ac46  subq.l #2 / move.w D0w (token) / pea (0x1384,A5) / jsr ML_$SPIN_UNLOCK / bra 0xE1AC7E
 *   -- 1 / 2 --
 *   00e1ac0e  A3 = A5 + line*0x38; D4 = (0x12cc,A3) alt_handler; bne -> 0xE1AC2C
 *   00e1ac24  move.l #0xb0007,(A1) / bra exit                    ; NO discipline store
 *   00e1ac2c  SPIN_LOCK as above; A0 = D4
 *   00e1ac3e  cmpi.w #1,D2w / bne -> 2; clr.l (A0); -> 0xE1AC46 unlock
 *   00e1ac56  2: (A0) = (0xc0,A5) ptr_tty_i_rcv_alt; SPIN_UNLOCK (addq #8)
 *   00e1ac6a  jsr DTTY_$RELOAD_FONT
 *   00e1ac70  pea (0x2a,PC) -> 0xE1AC9C TERM_$KBD_STRING_LEN / pea (0x1390,A5) /
 *             jsr TERM_$SEND_KBD_STRING                          ; args reclaimed by unlk
 *   -- store --
 *   00e1ac7e  A2 = A5 + line*0x38; (0x12d4,A2) = D2w              ; dtte.discipline
 *
 * The ML_$SPIN_UNLOCK token is D0w, which every arm has left untouched since
 * the lock call (only memory-to-memory moves intervene).
 */

#include "term/term_internal.h"

void TERM_$SET_REAL_LINE_DISCIPLINE(unsigned short *line_ptr, short *discipline_ptr,
                                     status_$t *status_ret)
{
    uint16_t line;              /* D3w */
    uint16_t discipline;        /* D2w */
    dtte_t *entry;
    sio_desc_t *desc;           /* A0 on the 0/3 arms */
    m68k_ptr_t *kbd_handler;    /* A0 on the 1/2 arms */
    ml_$spin_token_t token;     /* A6-0xE */

    /* 0x00E1AB78..0x00E1AB8E */
    line = *line_ptr;
    if (line > 3) {
        *status_ret = status_$invalid_line_number;
        return;
    }
    if (line >= (uint16_t)TERM_$MAX_DTTE) {
        *status_ret = status_$requested_line_or_operation_not_implemented;
        return;
    }

    /* 0x00E1AB92..0x00E1AB9C */
    *status_ret = status_$ok;
    discipline = (uint16_t)*discipline_ptr;
    entry = &DTTE[line];

    /* 0x00E1AB9E..0x00E1ABAC */
    switch (discipline) {
    case 0:
    case 3:
        /* 0x00E1ABB8..0x00E1ABCC */
        if (entry->tty_handler == 0) {
            *status_ret = status_$invalid_line_number;      /* 0x00E1AC24 */
            return;
        }
        /* 0x00E1ABCE..0x00E1ABDA */
        token = ML_$SPIN_LOCK(&TERM_$DATA.tty_spin_lock);
        desc = (sio_desc_t *)ARCH_VA_TO_PTR(entry->tty_handler);
        if (discipline == 0) {
            /* 0x00E1ABE4..0x00E1ABFC */
            desc->owner = entry->handler_ptr;
            desc->rcv_handler = TERM_$DATA.ptr_tty_i_rcv;
            desc->drain_handler = TERM_$DATA.ptr_tty_i_drain;
            desc->dcd_handler = TERM_$DATA.ptr_tty_i_hup;
            desc->special_rcv = TERM_$DATA.ptr_tty_i_int;
        } else {
            /* 0x00E1AC04 */
            desc->rcv_handler = ARCH_PTR_TO_VA(SUMA_$RCV);
        }
        /* 0x00E1AC46..0x00E1AC54 */
        ML_$SPIN_UNLOCK(&TERM_$DATA.tty_spin_lock, token);
        break;

    case 1:
    case 2:
        /* 0x00E1AC0E..0x00E1AC22 */
        if (entry->alt_handler == 0) {
            *status_ret = status_$invalid_line_number;      /* 0x00E1AC24 */
            return;
        }
        /* 0x00E1AC2C..0x00E1AC3C */
        token = ML_$SPIN_LOCK(&TERM_$DATA.tty_spin_lock);
        kbd_handler = (m68k_ptr_t *)ARCH_VA_TO_PTR(entry->alt_handler);
        if (discipline == 1) {
            /* 0x00E1AC44..0x00E1AC54 */
            *kbd_handler = 0;
            ML_$SPIN_UNLOCK(&TERM_$DATA.tty_spin_lock, token);
        } else {
            /* 0x00E1AC56..0x00E1AC78 */
            *kbd_handler = TERM_$DATA.ptr_tty_i_rcv_alt;
            ML_$SPIN_UNLOCK(&TERM_$DATA.tty_spin_lock, token);
            DTTY_$RELOAD_FONT();
            TERM_$SEND_KBD_STRING(TERM_$KBD_STRING_DATA, &TERM_$KBD_STRING_LEN);
        }
        break;

    default:
        /* 0x00E1ABA2: bcc.w 0x00e1ac7e - recorded without touching anything */
        break;
    }

    /* 0x00E1AC7E..0x00E1AC8E */
    entry->discipline = (int16_t)discipline;
}
