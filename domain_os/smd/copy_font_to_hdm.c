/*
 * smd/copy_font_to_hdm.c - SMD_$COPY_FONT_TO_HDM
 *
 * Re-emitted from the image.  The public entry 0x00E84934 is a 10-byte
 * gate (`lea (-0x2,PC),A0` / `jmp 0x00E702F4`) and the body runs
 * 0x00E702F4..0x00E70374 (132 bytes).  No link frame: after the eight-
 * register movem (32 bytes) the arguments sit at (0x24,SP) display_base
 * -> A3, (0x28,SP) font -> A2, (0x2C,SP) hdm_pos -> A4.  A5 = A0 (the gate
 * address) is never used.
 *
 *   00e702fe  cmpi.w #1,(A2)
 *   00e70304  v3: D0 = (0x2c,A2) long ; A2 += (0x28,A2) long
 *   00e7030e  v1: D0 = (0x8,A2) word ; A2 += (0x2,A2) word (adda.w)
 *   00e70316  D0 = ((D0 + 3) >> 2 (word)) - 1         ; longwords - 1
 *   00e70326  D1 = hdm_pos->y ; D3 = 0x3FF - y ; D1 <<= 7 (y * 128 bytes)
 *   00e70332  D4 = *(display_base + 0x1FFF0)          ; the XOR mask
 *   00e70338  D6 = 0x64
 *   00e7033e  A3 = display_base + y*128 + (hdm_pos->x >> 3)
 *   loop 00e70348:
 *     D2 = min(D0, 6) ; copy D2+1 longwords: *A3 = *A2++ ; *A3++ ^= D4
 *     A3 += 0x64                                       ; to the next row
 *     dbf D3 -> 00e7036c, else A3 -= 0x6FE4 ; D3 = 0xDF ; (HDM wrap)
 *     00e7036c  D0 -= 7 ; bge loop
 *
 * The old body advanced by 0x68 bytes per row and wrapped by 0x1BDF
 * longwords; the image uses 0x64 bytes (7 longwords + 0x64 = one 128-byte
 * scan line) and 0x6FE4 bytes.
 */

#include "smd/smd_internal.h"

#define SMD_XOR_MASK_OFFSET     0x1FFF0u    /* 0x00E70332 */
#define SMD_HDM_LAST_ROW        0x3FF       /* 0x00E7032A */
#define SMD_HDM_ROW_STRIDE      0x64        /* 0x00E70338: bytes skipped after 7 longwords */
#define SMD_HDM_WRAP_BACK       0x6FE4u     /* 0x00E70362 */
#define SMD_HDM_WRAP_ROWS       0xDF        /* 0x00E70368 */

void SMD_$COPY_FONT_TO_HDM(uint32_t display_base, void *font, smd_hdm_pos_t *hdm_pos)
{
    const uint8_t *src;              /* A2 */
    uint8_t *dst;                    /* A3 */
    uint32_t xor_mask;               /* D4 */
    int16_t words_left;              /* D0 (low word) */
    int16_t rows_left;               /* D3 */
    int16_t n;                       /* D2 */
    uint32_t w;
    int i;

    /* 0x00E702FE-0x00E70316 */
    if (((const smd_font_v1_t *)font)->version == SMD_FONT_VERSION_1) {
        const smd_font_v1_t *v1 = (const smd_font_v1_t *)font;
        words_left = (int16_t)v1->char_width;                       /* (0x8,A2) */
        src = (const uint8_t *)font + (int16_t)v1->data_offset;      /* adda.w (0x2,A2) */
    } else {
        const smd_font_v3_t *v3 = (const smd_font_v3_t *)font;
        words_left = (int16_t)v3->data_size;                        /* (0x2c,A2) */
        src = (const uint8_t *)font + v3->data_offset;               /* adda.l (0x28,A2) */
    }
    /* 0x00E70316-0x00E7031A: addq.l #3 ; lsr.w #2 ; subq.l #1 */
    words_left = (int16_t)((uint16_t)(words_left + 3) >> 2) - 1;

    /* 0x00E7031C-0x00E70346 */
    rows_left = (int16_t)(SMD_HDM_LAST_ROW - hdm_pos->y);
    xor_mask = *(const uint32_t *)ARCH_VA_TO_PTR(display_base + SMD_XOR_MASK_OFFSET);
    dst = (uint8_t *)ARCH_VA_TO_PTR(display_base + ((uint32_t)hdm_pos->y << 7)
                                    + (int16_t)(hdm_pos->x >> 3));

    /* 0x00E70348-0x00E7036E */
    do {
        n = words_left;
        if (n > 6) {
            n = 6;                                                   /* 0x00E70350 */
        }
        for (i = 0; i <= n; i++) {                                   /* dbf D2 */
            w = ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
                ((uint32_t)src[2] << 8) | (uint32_t)src[3];
            w ^= xor_mask;
            dst[0] = (uint8_t)(w >> 24); dst[1] = (uint8_t)(w >> 16);
            dst[2] = (uint8_t)(w >> 8);  dst[3] = (uint8_t)w;
            src += 4;
            dst += 4;
        }
        dst += SMD_HDM_ROW_STRIDE;                                   /* 0x00E7035C */
        rows_left--;                                                 /* dbf D3 */
        if (rows_left < 0) {
            dst -= SMD_HDM_WRAP_BACK;                                /* 0x00E70362 */
            rows_left = SMD_HDM_WRAP_ROWS;                           /* 0x00E70368 */
        }
        words_left = (int16_t)(words_left - 7);                      /* 0x00E7036C */
    } while (words_left >= 0);
}
