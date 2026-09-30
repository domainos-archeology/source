/*
 * net_io/test/test_copy_packet.c - unit tests for NET_IO_$COPY_PACKET
 * (0x00E0E514)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void reset_state(void);

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
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

#include "net_io/net_io_internal.h"
#include "netbuf/netbuf.h"
#include "os/os.h"

/* target VAs 0x10000.. map into the arena */
static uint8_t arena[0x4000] __attribute__((aligned(16)));
#define VA(off) (0x10000u + (off))

static int n_get_dat, n_rtn_dat, n_getva, n_rtnva, n_hdr;
static int getva_fail_at;
static uint32_t rtn_dat[8];

void NETBUF_$GET_DAT(uint32_t *addr_out) { *addr_out = 0x100u + (uint32_t)n_get_dat++; }
void NETBUF_$RTN_DAT(uint32_t addr) { rtn_dat[n_rtn_dat++] = addr; }
void NETBUF_$GETVA(uint32_t ppn, uint32_t *va_out, status_$t *status)
{
    /* data page 0x10n -> VA(0x1000 + n*0x400); source page 0x20n -> VA(0x3000 + ...) */
    if (ppn >= 0x200) *va_out = VA(0x3000 + (ppn - 0x200) * 0x400);
    else *va_out = VA(0x1000 + (ppn - 0x100) * 0x400);
    *status = (n_getva == getva_fail_at) ? 0x00110001 : 0;
    n_getva++;
}
uint32_t NETBUF_$RTNVA(uint32_t *va_ptr) { (void)va_ptr; n_rtnva++; return 0; }
void NETBUF_$GET_HDR(uint32_t *phys_out, uint32_t *va_out)
{
    n_hdr++; *phys_out = 0x77; *va_out = VA(0x800);
}
void OS_$DATA_COPY(const void *src, void *dst, uint32_t len) { memcpy(dst, src, len); }

#include "../copy_packet.c"

static uint32_t hdr_src, hdr_out, pages_out[4];
static status_$t st;

static void reset_state(void)
{
    int i;
    memset(arena, 0, sizeof(arena));
    ARCH_HOST_VA_BASE = (uintptr_t)arena - 0x10000u;
    n_get_dat = n_rtn_dat = n_getva = n_rtnva = n_hdr = 0;
    getva_fail_at = -1;
    for (i = 0; i < 0x20; i++) arena[i] = (uint8_t)(0xA0 + i);       /* header */
    for (i = 0; i < 0x800; i++) arena[0x2000 + i] = (uint8_t)i;     /* linear src */
    for (i = 0; i < 0x400; i++) arena[0x3000 + i] = 0x5A;            /* src page */
    hdr_src = VA(0);
    hdr_out = 0;
    memset(pages_out, 0xEE, sizeof(pages_out));
    st = -1;
}

static void test_no_payload(void)
{
    NET_IO_$COPY_PACKET(&hdr_src, 0x10, 0, NULL, 0, &hdr_out, pages_out, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, pages_out[0]);
    ASSERT_EQ(VA(0x800), hdr_out);
    ASSERT_EQ(0xA5, arena[0x805]);
    ASSERT_EQ(0, arena[0x810]);
}

static void test_linear_two_pages(void)
{
    NET_IO_$COPY_PACKET(&hdr_src, 0x8, VA(0x2000), NULL, 0x500, &hdr_out,
                        pages_out, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(2, n_get_dat);
    ASSERT_EQ(0x100, pages_out[0]);
    ASSERT_EQ(0x101, pages_out[1]);
    ASSERT_EQ(0x10, arena[0x1010]);
    ASSERT_EQ(0x4FF & 0xFF, arena[0x1400 + 0xFF]);
    ASSERT_EQ(0, arena[0x1400 + 0x100]);   /* only 0x100 bytes of page 2 */
    ASSERT_EQ(2, n_rtnva);
    ASSERT_EQ(1, n_hdr);
}

static void test_page_source(void)
{
    uint32_t src_pages[1] = { 0x200 };
    NET_IO_$COPY_PACKET(&hdr_src, 0x8, 0, src_pages, 0x10, &hdr_out,
                        pages_out, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0x5A, arena[0x100F]);
    ASSERT_EQ(0, arena[0x1010]);
    ASSERT_EQ(2, n_getva);
    ASSERT_EQ(2, n_rtnva);
}

static void test_dest_mapping_fails(void)
{
    getva_fail_at = 1;                  /* second page's destination (linear: one GETVA per page) */
    NET_IO_$COPY_PACKET(&hdr_src, 0x8, VA(0x2000), NULL, 0x800, &hdr_out,
                        pages_out, &st);
    ASSERT_EQ(0x00110004, st);
    ASSERT_EQ(2, n_rtn_dat);
    ASSERT_EQ(0x100, rtn_dat[0]);
    ASSERT_EQ(0x101, rtn_dat[1]);
    ASSERT_EQ(2, n_rtnva);              /* page 1's, and page 2's non-zero VA */
    ASSERT_EQ(0, n_hdr);
    ASSERT_EQ(0, hdr_out);
}

static void test_source_mapping_fails(void)
{
    uint32_t src_pages[1] = { 0x200 };
    getva_fail_at = 1;
    NET_IO_$COPY_PACKET(&hdr_src, 0x8, 0, src_pages, 0x10, &hdr_out,
                        pages_out, &st);
    ASSERT_EQ(0x00110004, st);
    ASSERT_EQ(1, n_rtn_dat);
    ASSERT_EQ(2, n_rtnva);
    ASSERT_EQ(0, n_hdr);
}

int main(void)
{
    printf("NET_IO_$COPY_PACKET tests\n");
    RUN_TEST(no_payload);
    RUN_TEST(linear_two_pages);
    RUN_TEST(page_source);
    RUN_TEST(dest_mapping_fails);
    RUN_TEST(source_mapping_fails);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
