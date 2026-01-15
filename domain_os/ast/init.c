/*
 * AST_$INIT - Initialize AST subsystem
 *
 * Calculates the number of AOTEs and ASTEs based on available
 * physical memory and initializes the tables.
 *
 * Original address: 0x00e2f000
 */

#include "ast/ast_internal.h"
#include "misc/misc.h"

void AST_$INIT(void)
{
    int16_t aote_count;
    int16_t aste_count;
    int32_t temp;
    status_$t status;

    /* Calculate number of ASTEs based on physical pages
     * Formula: (REAL_PAGES + 0x1FF) >> 9 gives number of 512-page blocks
     * Then multiply to get ASTE count */
    int16_t blocks = (int16_t)((MMAP_$REAL_PAGES + 0x1FF) >> 9);

    /* Calculate ASTE count: blocks * 0x50 + 0x280 */
    aste_count = blocks * 0x50 + 0x280;

    /* Calculate AOTE count based on ASTE count
     * Formula involves rounding to page boundaries */
    temp = (int32_t)(blocks * 0x28) * 0xC0;  /* AOTE size is 0xC0 */
    temp = (temp + 0x3FF);
    if (temp < 0) {
        temp += 0x3FF;  /* Handle negative rounding */
    }
    temp >>= 10;  /* Divide by 1024 (page size) */
    temp <<= 10;  /* Round down to page boundary */
    aote_count = (int16_t)(temp / 0xC0);

    /* Clamp AOTE count to maximum */
    if (aote_count > AST_MAX_AOTE) {
        aote_count = AST_MAX_AOTE;
    }

    /* Add the AOTEs */
    AST_$ADD_AOTES((uint16_t*)&aote_count, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /* Clamp ASTE count to maximum */
    if (aste_count > AST_MAX_ASTE) {
        aste_count = AST_MAX_ASTE;
    }

    /* Add the ASTEs */
    AST_$ADD_ASTES((uint16_t*)&aste_count, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }
}
