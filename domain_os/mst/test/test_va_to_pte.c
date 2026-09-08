/*
 * mst/test/test_va_to_pte.c - Unit tests for mst_$va_to_pte (0x00E4411C)
 *
 * Bead source-ylxp.  The entry address the original forms is
 *
 *   0xEF6400 (MSTE_PAGES, `movea.l #0xef6400,A1` at 0x00E4417A)
 *   + MST[..] * 0x400        (0x00E44186 lsl.l #8 / lsl.l #2, 0x00E4418A lea)
 *   + (local_seg & 0x3f) * 16 (0x00E4418E moveq #0x3f / 0x00E44196 lsl.w #4)
 *   - 0x400                   (0x00E4419C lea (-0x400,A1),A1)
 *
 * so the MST word is a ONE-BASED page number and page 1 lands exactly on
 * MSTE_PAGES.  These tests pin that, plus the two failure arms and the
 * protection-byte extraction at 0x00E441B0.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_mocks(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n    Assertion failed at line %d: %s\n", __LINE__, #cond); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "mst/mst_internal.h"

/* ---- data the function reaches through ------------------------------- */

uint16_t MST[MST_TABLE_ENTRIES];          /* 0xEE5800 */
uint16_t MST_ASID_BASE[MST_MAX_ASIDS];    /* 0xE243D4 */

/*
 * Four page-table pages of 0x400 bytes each, standing in for MSTE_PAGES.
 * ARCH_HOST_VA_BASE is set so that VA 0xEF6400 is pte_pages[0].
 */
static uint8_t pte_pages[4 * 0x400] __attribute__((aligned(0x400)));

/* ---- mocked callee ---------------------------------------------------- */

static uint16_t mock_segno;
static uint16_t mock_local_seg;
static int mock_segno_calls;
static uint32_t mock_segno_va;
static uint16_t mock_segno_asid;

uint16_t MST_$VA_TO_SEGNO(uint32_t virtual_addr, uint16_t *segno_out,
                          uint16_t default_result)
{
    mock_segno_calls++;
    mock_segno_va = virtual_addr;
    mock_segno_asid = default_result;
    *segno_out = mock_local_seg;
    return mock_segno;
}

static void reset_mocks(void)
{
    memset(MST, 0, sizeof(MST));
    memset(MST_ASID_BASE, 0, sizeof(MST_ASID_BASE));
    memset(pte_pages, 0, sizeof(pte_pages));
    mock_segno = 0;
    mock_local_seg = 0;
    mock_segno_calls = 0;
    mock_segno_va = 0;
    mock_segno_asid = 0;
    ARCH_HOST_VA_BASE = (uintptr_t)pte_pages - (uintptr_t)MST_PAGE_TABLE_BASE;
}

#include "../va_to_pte.c"

/* ---------------------------------------------------------------------- */

/*
 * Page number 1 with sub-index 0 must resolve to MSTE_PAGES itself, not to
 * MSTE_PAGES + 0x400: the `lea (-0x400,A1)` is part of the address, not a
 * fixup applied after the store.
 */
static void test_page_one_is_the_base_page(void)
{
    uint16_t prot = 0xdead;
    void *entry = (void *)0x1234;
    status_$t st = 0x5a5a5a5a;

    mock_segno = 0;
    mock_local_seg = 0;          /* dir index 0, sub-index 0 */
    MST_ASID_BASE[0] = 0;
    MST[0] = 1;                  /* one-based page number */
    *(uint32_t *)&pte_pages[0] = 0x11223344;
    pte_pages[0x0A] = 0x00;

    mst_$va_to_pte(7, 0x12345678u, &prot, &entry, &st);

    ASSERT_EQ(1, mock_segno_calls);
    ASSERT_EQ(0x12345678u, mock_segno_va);
    ASSERT_EQ(7, mock_segno_asid);
    ASSERT_EQ((uintptr_t)&pte_pages[0], (uintptr_t)entry);
    ASSERT_EQ(0, st);
}

/* page 2 is the second 0x400-byte page, and the sub-index scales by 16 */
static void test_page_and_sub_index_arithmetic(void)
{
    uint16_t prot = 0;
    void *entry = 0;
    status_$t st = 1;

    mock_segno = 0x39;                       /* the largest accepted segno */
    mock_local_seg = (uint16_t)((1u << 6) | 3u); /* dir +1, sub-index 3 */
    MST_ASID_BASE[0x39] = 4;                 /* word index 4 + 1 = 5 */
    MST[5] = 2;
    *(uint32_t *)&pte_pages[0x400 + 3 * 16] = 0xffffffffu;

    mst_$va_to_pte(0, 0, &prot, &entry, &st);

    ASSERT_EQ((uintptr_t)&pte_pages[0x400 + 3 * 16], (uintptr_t)entry);
    ASSERT_EQ(0, st);
}

/* moveq #0x3e / and.b (0xa,A1),D0b / lsr.w #1 */
static void test_protection_byte(void)
{
    uint16_t prot = 0;
    void *entry = 0;
    status_$t st = 1;

    mock_segno = 0;
    mock_local_seg = 0;
    MST[0] = 1;
    *(uint32_t *)&pte_pages[0] = 1;
    pte_pages[0x0A] = 0xFF;      /* 0xFF & 0x3E = 0x3E, >> 1 = 0x1F */

    mst_$va_to_pte(0, 0, &prot, &entry, &st);
    ASSERT_EQ(0x1F, prot);
    ASSERT_EQ(0, st);

    pte_pages[0x0A] = 0x14;      /* 0x14 & 0x3E = 0x14, >> 1 = 0x0A */
    mst_$va_to_pte(0, 0, &prot, &entry, &st);
    ASSERT_EQ(0x0A, prot);

    pte_pages[0x0A] = 0x81;      /* both bits outside the mask */
    mst_$va_to_pte(0, 0, &prot, &entry, &st);
    ASSERT_EQ(0x00, prot);
}

/* cmpi.w #0x39,D0w / bhi.b 0x00E4416E: entry_out is NOT touched on this arm */
static void test_segno_out_of_range(void)
{
    uint16_t prot = 0xbeef;
    void *entry = (void *)0x1234;
    status_$t st = 0;

    mock_segno = 0x3A;
    mst_$va_to_pte(0, 0, &prot, &entry, &st);

    ASSERT_EQ(status_$reference_to_illegal_address, st);
    ASSERT_EQ(0x1234, (uintptr_t)entry);
    ASSERT_EQ(0xbeef, prot);
}

/* tst.w (0x0,A1,D0w*0x1) / beq: no page-table page, entry_out untouched */
static void test_no_page_table_page(void)
{
    uint16_t prot = 0xbeef;
    void *entry = (void *)0x1234;
    status_$t st = 0;

    mock_segno = 0;
    mock_local_seg = 0;
    MST[0] = 0;

    mst_$va_to_pte(0, 0, &prot, &entry, &st);

    ASSERT_EQ(status_$reference_to_illegal_address, st);
    ASSERT_EQ(0x1234, (uintptr_t)entry);
}

/* tst.l (A1) / beq: the entry is stored first, then cleared (clr.l (A3)) */
static void test_empty_entry_clears_entry_out(void)
{
    uint16_t prot = 0xbeef;
    void *entry = (void *)0x1234;
    status_$t st = 0;

    mock_segno = 0;
    mock_local_seg = 5;
    MST[0] = 1;
    *(uint32_t *)&pte_pages[5 * 16] = 0;   /* empty entry */

    mst_$va_to_pte(0, 0, &prot, &entry, &st);

    ASSERT_EQ(status_$reference_to_illegal_address, st);
    ASSERT_EQ(0, (uintptr_t)entry);
    ASSERT_EQ(0xbeef, prot);                /* prot_out is never written */
}

/*
 * The dir index is the SUM of MST_ASID_BASE[segno] and local_seg >> 6, formed
 * in word arithmetic (0x00E4415E add.w (0x0,A0,D1w*0x1),D3w).
 */
static void test_dir_index_is_asid_base_plus_seg_shift(void)
{
    uint16_t prot = 0;
    void *entry = 0;
    status_$t st = 1;

    mock_segno = 2;
    MST_ASID_BASE[2] = 0x10;
    mock_local_seg = (uint16_t)(3u << 6);   /* 3 + 0x10 = 0x13 */
    MST[0x13] = 3;
    *(uint32_t *)&pte_pages[2 * 0x400] = 0x1;

    mst_$va_to_pte(0, 0, &prot, &entry, &st);

    ASSERT_EQ((uintptr_t)&pte_pages[2 * 0x400], (uintptr_t)entry);
    ASSERT_EQ(0, st);
}

int main(void)
{
    printf("mst_$va_to_pte tests:\n");
    RUN_TEST(page_one_is_the_base_page);
    RUN_TEST(page_and_sub_index_arithmetic);
    RUN_TEST(protection_byte);
    RUN_TEST(segno_out_of_range);
    RUN_TEST(no_page_table_page);
    RUN_TEST(empty_entry_clears_entry_out);
    RUN_TEST(dir_index_is_asid_base_plus_seg_shift);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
