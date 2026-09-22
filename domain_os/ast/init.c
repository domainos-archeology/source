/*
 * AST_$INIT - Size and build the AOTE and ASTE pools
 *
 * With blocks = (MMAP_$REAL_PAGES + 0x1FF) >> 9 (the number of 512-page
 * units of real memory):
 *
 *   aste_count = blocks * 0x50 + 0x280            (16-bit arithmetic)
 *   aote_count = ((((blocks * 0x28) * 0xC0 + 0x3FF, +0x3FF again when
 *                 negative) >> 10) as a word) << 10) / 0xC0
 *                i.e. blocks*40 AOTEs rounded up to whole 1KB pages and
 *                then back to whole AOTEs
 *
 * each clamped to the table maximum, then handed to AST_$ADD_AOTES and
 * AST_$ADD_ASTES; a non-zero status from either crashes the system.
 *
 * Original address: 0x00E2F000 (194 bytes).  No A5 is loaded; the counts
 * live at (-0xA,A6) and (-0xC,A6), the status at (-0x4,A6).
 */

#include "ast/ast_internal.h"
#include "misc/misc.h"

void AST_$INIT(void)
{
    uint32_t blocks;        /* D2 (longword) */
    int16_t aste_count;     /* D2w after 0x00E2F01A */
    int16_t aote_count;     /* D3w */
    int32_t bytes;          /* D3 */
    int16_t pages;          /* D3w after the shift */
    int16_t count_cell;     /* (-0xA,A6) / (-0xC,A6) */
    status_$t status;       /* (-0x4,A6) */

    /* 0x00E2F008..0x00E2F016: (REAL_PAGES + 0x1FF) >> 9, unsigned */
    blocks = (MMAP_$REAL_PAGES + 0x1FF) >> 9;

    /*
     * 0x00E2F018..0x00E2F02C, all on the low words:
     *   D2w = blocks*16, D1w = blocks*8, D1w += D1w*4 (blocks*40),
     *   D2w += D2w*4 (blocks*80), D3w = D1w, D2w += 0x280.
     */
    aste_count = (int16_t)((int16_t)((uint16_t)blocks << 4) * 5 + 0x280);
    pages = (int16_t)((int16_t)((uint16_t)blocks << 3) * 5);

    /*
     * 0x00E2F030..0x00E2F048: ext.l; *64; *3 (= *0xC0); + 0x3FF; a negative
     * total gets 0x3FF added once more; asr #8 then asr #2 (= /1024).
     */
    bytes = (int32_t)pages * 0xC0;
    bytes += 0x3FF;
    if (bytes < 0) {
        bytes += 0x3FF;
    }
    bytes >>= 10;

    /* 0x00E2F050..0x00E2F062: the WORD of that, ext.l, << 10, divs.w #0xC0
     * (16-bit quotient), then clamp at 0x118 (D0w from 0x00E2F04A) */
    aote_count = (int16_t)((((int32_t)(int16_t)bytes) << 10) / 0xC0);
    if (aote_count > AST_MAX_AOTE) {
        aote_count = AST_MAX_AOTE;
    }

    /* 0x00E2F064..0x00E2F088 */
    count_cell = aote_count;
    (void)AST_$ADD_AOTES((uint16_t *)&count_cell, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /* 0x00E2F08A..0x00E2F094: clamp at 0x1F8 */
    if (aste_count > AST_MAX_ASTE) {
        aste_count = AST_MAX_ASTE;
    }

    /* 0x00E2F094..0x00E2F0B2 */
    count_cell = aste_count;
    (void)AST_$ADD_ASTES((uint16_t *)&count_cell, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }
}
