/*
 * ast/test/test_fetch_pmap_page.c - Unit tests for AST_$FETCH_PMAP_PAGE
 *                                   (0x00E041A8)
 *
 * The test #includes ast/fetch_pmap_page.c directly and drives the real
 * routine through mocked callees.  It pins:
 *
 *   - one page is allocated (count 1, min 1) under the PMAP lock and its
 *     data buffer (ppn << 10) returned to NETBUF;
 *   - NETWORK_$READ_AHEAD receives &AREA_$PARTNER, the caller's request,
 *     the ppn array, the flags word as page_size, count 1, and ONE clock
 *     cell for all three timestamp outputs;
 *   - on success the page is mapped at AST_$ZERO_BUFF with flags 0x16,
 *     256 longwords are copied, and the page is removed and freed;
 *   - on failure the buffer is fetched back and freed by ppn.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define uid_t ast_uid_t

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

static void reset_state(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %-48s", #name);                                         \
    current_failed = 0;                                                       \
    reset_state();                                                            \
    test_##name();                                                            \
    if (current_failed == 0) { tests_passed++; printf("PASSED\n"); }          \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    if ((unsigned long long)(expected) != (unsigned long long)(actual)) {      \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",       \
               (unsigned long long)(expected),                                \
               (unsigned long long)(actual), __LINE__);                       \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

#include "ast/fetch_pmap_page.c"

uint32_t AST_$ZERO_BUFF[256];
area_$globals_t AREA_$GLOBALS;

static int lock_calls, unlock_calls;
void ML_$LOCK(int16_t id)   { lock_calls++; (void)id; }
void ML_$UNLOCK(int16_t id) { unlock_calls++; (void)id; }

static int16_t alloc_count, alloc_min;
static uint32_t alloc_ppn = 0x321;
int16_t ast_$allocate_pages(int16_t count, int16_t min_count, uint32_t *ppn_array)
{
    alloc_count = count; alloc_min = min_count;
    ppn_array[0] = alloc_ppn;
    return count;
}

static uint32_t rtn_dat_addr;
void NETBUF_$RTN_DAT(uint32_t addr) { rtn_dat_addr = addr; }

static int get_dat_calls;
static uint32_t get_dat_value = 0x77 << 10;
void NETBUF_$GET_DAT(uint32_t *addr_out) { get_dat_calls++; *addr_out = get_dat_value; }

static void *ra_net_info, *ra_uid;
static uint32_t *ra_ppn_array;
static uint16_t ra_page_size;
static int16_t ra_count;
static int8_t ra_no_ra;
static uint8_t ra_flags;
static clock_t *ra_dtm, *ra_clock, *ra_acl;
static status_$t ra_status;
int16_t NETWORK_$READ_AHEAD(void *net_info, void *uid, uint32_t *ppn_array,
                            uint16_t page_size, int16_t count,
                            int8_t no_read_ahead, uint8_t flags,
                            clock_t *dtm, clock_t *clock, clock_t *acl_info,
                            status_$t *status)
{
    ra_net_info = net_info; ra_uid = uid; ra_ppn_array = ppn_array;
    ra_page_size = page_size; ra_count = count; ra_no_ra = no_read_ahead;
    ra_flags = flags; ra_dtm = dtm; ra_clock = clock; ra_acl = acl_info;
    *status = ra_status;
    /* the "page" arrives in the zero buffer */
    for (int i = 0; i < 256; i++) { AST_$ZERO_BUFF[i] = 0x1000 + i; }
    return 1;
}

static uint32_t install_ppn, install_va, install_flags;
void MMU_$INSTALL(uint32_t ppn, uint32_t va, uint32_t flags)
{
    install_ppn = ppn; install_va = va; install_flags = flags;
}
static uint32_t remove_ppn;
void MMU_$REMOVE(uint32_t ppn) { remove_ppn = ppn; }
static int free_calls;
static uint32_t free_vpn;
void MMAP_$FREE(uint32_t vpn) { free_calls++; free_vpn = vpn; }

static void reset_state(void)
{
    lock_calls = unlock_calls = 0;
    alloc_count = alloc_min = -1;
    rtn_dat_addr = 0;
    get_dat_calls = 0;
    ra_status = status_$ok;
    ra_net_info = ra_uid = NULL; ra_ppn_array = NULL;
    install_ppn = install_va = install_flags = 0;
    remove_ppn = 0; free_calls = 0; free_vpn = 0;
    memset(AST_$ZERO_BUFF, 0, sizeof(AST_$ZERO_BUFF));
}

TEST(success_copies_page_and_frees)
{
    uint32_t out[256];
    uint8_t req[32];
    status_$t status = 0x5555;

    memset(out, 0xEE, sizeof(out));
    AST_$FETCH_PMAP_PAGE(req, out, 0x400, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, alloc_count);
    ASSERT_EQ(1, alloc_min);
    ASSERT_EQ(0x321u << 10, rtn_dat_addr);
    ASSERT_EQ((uintptr_t)&AREA_$PARTNER, (uintptr_t)ra_net_info);
    ASSERT_EQ((uintptr_t)req, (uintptr_t)ra_uid);
    ASSERT_EQ(0x400, ra_page_size);
    ASSERT_EQ(1, ra_count);
    ASSERT_EQ(0, ra_no_ra);
    ASSERT_EQ(0, ra_flags);
    ASSERT_EQ(0x321, ra_ppn_array[0]);
    /* one scratch cell, three times */
    ASSERT_EQ((uintptr_t)ra_dtm, (uintptr_t)ra_clock);
    ASSERT_EQ((uintptr_t)ra_dtm, (uintptr_t)ra_acl);
    ASSERT_EQ(0x321, install_ppn);
    ASSERT_EQ(ARCH_PTR_TO_VA(AST_$ZERO_BUFF), install_va);
    ASSERT_EQ(0x16, install_flags);
    ASSERT_EQ(0x1000, out[0]);
    ASSERT_EQ(0x10FF, out[255]);
    ASSERT_EQ(0x321, remove_ppn);
    ASSERT_EQ(1, free_calls);
    ASSERT_EQ(0x321, free_vpn);
    ASSERT_EQ(0, get_dat_calls);
    ASSERT_EQ(2, lock_calls);
    ASSERT_EQ(2, unlock_calls);
}

TEST(failure_returns_buffer_by_ppn)
{
    uint32_t out[256];
    uint8_t req[32];
    status_$t status = 0;

    ra_status = 0x000F0001;
    memset(out, 0xEE, sizeof(out));
    AST_$FETCH_PMAP_PAGE(req, out, 0x400, &status);

    ASSERT_EQ(0x000F0001, status);
    ASSERT_EQ(1, get_dat_calls);
    ASSERT_EQ(1, free_calls);
    ASSERT_EQ(0x77, free_vpn);
    ASSERT_EQ(0, install_ppn);
    ASSERT_EQ(0xEEEEEEEEu, out[0]);          /* untouched */
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
}

int main(void)
{
    printf("test_fetch_pmap_page (AST_$FETCH_PMAP_PAGE 0x00E041A8)\n");

    RUN_TEST(success_copies_page_and_frees);
    RUN_TEST(failure_returns_buffer_by_ppn);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
