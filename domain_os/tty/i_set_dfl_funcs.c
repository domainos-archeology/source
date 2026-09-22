/*
 * tty_$i_set_funcs / TTY_$I_SET_DFL_FUNCS - (re)bind function characters
 *
 * Both live in the TTY module at 0x00E671DC (map: "I E671DC TTY size = BC0")
 * whose data segment is A5 = 0x00E8242C ("D E8242C TTY size = 2C"):
 *   +0x00  tty_$i_dfl_func_classes[18]   function index -> character class
 *   +0x24  tty_$i_dfl_func_enable_mask   0x0001FFFF
 *   +0x28  DAT_00e82454                  0x000000D0 (break-mode mask)
 *
 * tty_$i_set_funcs, 0x00E6720E..0x00E6726C (96 bytes), module-local (the
 * map exports no symbol for it).  Arguments: tty (0x8,A6), mask longword
 * (0xc,A6), use_dfl byte in the high half of (0x10,A6).  A5 is inherited
 * from the caller (every caller loads 0x00E8242C first).
 *   0x00E67222  moveq #0x11 + dbf: 18 iterations, D3w = index, A1 walks the
 *               class table two bytes at a time
 *   0x00E67228  mask bit i clear -> nothing for this slot
 *   0x00E6722C  use_dfl < 0 and tty->func_enabled bit i set ->
 *               char_class[func_chars[i]] = dfl_classes[i]   (0x00E67244)
 *   0x00E6724A  otherwise char_class[func_chars[i]] = 0x12    (0x00E67256)
 *
 * TTY_$I_SET_DFL_FUNCS, 0x00E6726E..0x00E67290 (36 bytes):
 *   tty_$i_set_funcs(tty, tty_$i_dfl_func_enable_mask, use_dfl), with the
 *   mask read as a longword from (0x24,A5).
 *
 * Original addresses: 0x00e6720e, 0x00e6726e
 */

#include "tty/tty_internal.h"

void tty_$i_set_funcs(tty_desc_t *tty, uint32_t func_mask, char use_dfl)
{
    int16_t i;                                  /* D3w */
    uint8_t fc;                                 /* D4b */

    for (i = 0; i < TTY_MAX_FUNC_CHARS; i++) {  /* 0x00E67222 dbf #0x11 */
        if ((func_mask & (1uL << i)) == 0) {    /* 0x00E67228 btst.l D3,D0 */
            continue;
        }
        fc = tty->func_chars[i];
        if (use_dfl < 0 &&                      /* 0x00E6722C */
            (tty->func_enabled & (1uL << i)) != 0) {   /* 0x00E67230 */
            tty->char_class[fc] = tty_$i_dfl_func_classes[i];   /* 0x00E67244 */
        } else {
            tty->char_class[fc] = TTY_CHAR_CLASS_NORMAL;        /* 0x00E67256 */
        }
    }
}

void TTY_$I_SET_DFL_FUNCS(tty_desc_t *tty, char use_dfl)
{
    tty_$i_set_funcs(tty, tty_$i_dfl_func_enable_mask, use_dfl);   /* 0x00E6727A..0x00E67288 */
}
