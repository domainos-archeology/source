/*
 * Tests for CHKSUM_$GET_CHKSUM (0x00E0A314).
 *
 * chksum/get_chksum.c is #included below and the real function is called.
 *
 * The properties under test come straight from the seven instructions:
 *
 *   00e0a314  movea.l (0x4,SP),A0   ; the argument, read off the stack
 *   00e0a318  moveq #0x0,D0         ; sum starts at 0
 *   00e0a31a  move.w #0xff,D1w      ; 256 dbf iterations...
 *   00e0a31e  add.w (A0)+,D0w       ; ...of two words each = 512 words
 *   00e0a320  add.w (A0)+,D0w
 *   00e0a322  dbf D1w,0x00e0a31e
 *   00e0a326  rts                   ; result in D0
 *
 * i.e. exactly 1024 bytes are read, the words are big-endian pairs, and
 * the sum wraps modulo 2^16 because the adds are `add.w`.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "base/base.h"

/* ------------------------------------------------------------------ */
/* Code under test                                                     */
/* ------------------------------------------------------------------ */

#include "chksum/get_chksum.c"

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

/* A page plus a guard region, so "reads exactly 1024 bytes" is testable. */
static uint8_t page[CHKSUM_PAGE_SIZE + 16];

static void fill_words(uint16_t first, uint16_t step)
{
    unsigned i;
    uint16_t w = first;

    memset(page, 0, sizeof page);
    for (i = 0; i < CHKSUM_PAGE_WORDS; i++) {
        page[2 * i]     = (uint8_t)(w >> 8);
        page[2 * i + 1] = (uint8_t)w;
        w = (uint16_t)(w + step);
    }
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/* A zeroed page sums to zero (`moveq #0,D0` and nothing added). */
static void test_zero_page(void)
{
    memset(page, 0, sizeof page);
    assert(CHKSUM_$GET_CHKSUM(page) == 0);

    printf("test_zero_page: PASSED\n");
}

/*
 * Every word 1 -> 512 words summed -> 512.  This is the direct count of
 * `dbf` iterations times two.
 */
static void test_counts_512_words(void)
{
    fill_words(1, 0);
    assert(CHKSUM_$GET_CHKSUM(page) == CHKSUM_PAGE_WORDS);
    assert(CHKSUM_PAGE_WORDS == 512);

    printf("test_counts_512_words: PASSED\n");
}

/*
 * The routine reads exactly CHKSUM_PAGE_SIZE bytes: bytes past the page
 * must not change the answer, and the last word of the page must.
 */
static void test_reads_exactly_one_page(void)
{
    uint16_t base_sum;

    fill_words(0, 0);
    base_sum = CHKSUM_$GET_CHKSUM(page);
    assert(base_sum == 0);

    /* poison the guard bytes just past the page */
    memset(page + CHKSUM_PAGE_SIZE, 0xFF, 16);
    assert(CHKSUM_$GET_CHKSUM(page) == base_sum);

    /* the page's very last word does count */
    page[CHKSUM_PAGE_SIZE - 2] = 0x12;
    page[CHKSUM_PAGE_SIZE - 1] = 0x34;
    assert(CHKSUM_$GET_CHKSUM(page) == 0x1234);

    /* and so does its very first */
    page[0] = 0x00;
    page[1] = 0x01;
    assert(CHKSUM_$GET_CHKSUM(page) == 0x1235);

    printf("test_reads_exactly_one_page: PASSED\n");
}

/*
 * Words are BIG-endian pairs: 0x1234 is the bytes 0x12,0x34 in that order,
 * whatever the host's byte order is.
 */
static void test_words_are_big_endian(void)
{
    memset(page, 0, sizeof page);
    page[0] = 0x12;
    page[1] = 0x34;
    assert(CHKSUM_$GET_CHKSUM(page) == 0x1234);

    memset(page, 0, sizeof page);
    page[0] = 0x34;
    page[1] = 0x12;
    assert(CHKSUM_$GET_CHKSUM(page) == 0x3412);

    printf("test_words_are_big_endian: PASSED\n");
}

/*
 * `add.w` discards the carry out of bit 15: two words of 0x8000 sum to 0,
 * and 0xFFFF + 0x0002 is 0x0001.
 */
static void test_wraps_modulo_65536(void)
{
    memset(page, 0, sizeof page);
    page[0] = 0x80;
    page[2] = 0x80;
    assert(CHKSUM_$GET_CHKSUM(page) == 0x0000);

    memset(page, 0, sizeof page);
    page[0] = 0xFF; page[1] = 0xFF;
    page[2] = 0x00; page[3] = 0x02;
    assert(CHKSUM_$GET_CHKSUM(page) == 0x0001);

    /* every word 0xFFFF: 512 * 0xFFFF mod 0x10000 = 0x10000 - 512 */
    fill_words(0xFFFF, 0);
    assert(CHKSUM_$GET_CHKSUM(page) == (uint16_t)(0u - CHKSUM_PAGE_WORDS));

    printf("test_wraps_modulo_65536: PASSED\n");
}

/*
 * The sum is order-independent, so a page and its word-wise reversal
 * check the same.  This catches an off-by-one at either end of the loop.
 */
static void test_sum_is_order_independent(void)
{
    static uint8_t reversed[CHKSUM_PAGE_SIZE + 16];
    uint16_t forward;
    unsigned i;

    fill_words(0x0100, 0x0007);
    forward = CHKSUM_$GET_CHKSUM(page);

    memset(reversed, 0, sizeof reversed);
    for (i = 0; i < CHKSUM_PAGE_WORDS; i++) {
        unsigned src = CHKSUM_PAGE_WORDS - 1 - i;
        reversed[2 * i]     = page[2 * src];
        reversed[2 * i + 1] = page[2 * src + 1];
    }

    assert(CHKSUM_$GET_CHKSUM(reversed) == forward);

    printf("test_sum_is_order_independent: PASSED\n");
}

int main(void)
{
    printf("Running CHKSUM_$GET_CHKSUM tests...\n\n");

    test_zero_page();
    test_counts_512_words();
    test_reads_exactly_one_page();
    test_words_are_big_endian();
    test_wraps_modulo_65536();
    test_sum_is_order_independent();

    printf("\nAll tests PASSED!\n");
    return 0;
}
