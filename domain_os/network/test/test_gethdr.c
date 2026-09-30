/*
 * network/test/test_gethdr.c - NETWORK_$GETHDR (0x00E0F37A) and
 * NETWORK_$RTNHDR (0x00E0F414)
 *
 * Pins: loopback or a local destination takes a private wired page
 * (WP_$CALLOC, pa = ppn << 10, NETBUF_$GETVA, crash on a bad status);
 * otherwise the shared page under lock 0x18, released at once when the page
 * is nil.  RTNHDR frees a private page through NETBUF_$RTNVA/MMAP_$FREE and
 * unlocks for the shared one.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { tests_passed++; printf("PASSED\n"); } \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "network/network_internal.h"
#include "wp/wp.h"
#include "netbuf/netbuf.h"
#include "ml/ml.h"
#include "node/node.h"
#include "mmap/mmap.h"
#include "misc/crash_system.h"

int8_t NETWORK_$LOOPBACK_FLAG;
uint32_t NETWORK_$HDR_PAGE_PA;
uint32_t NETWORK_$HDR_PAGE;
uint32_t NODE_$ME;

static char trace[32];
static int nt;
static status_$t getva_status;
static uint32_t getva_pa, freed;

void WP_$CALLOC(uint32_t *ppn, status_$t *st) { trace[nt++] = 'C'; *ppn = 0x123; *st = 0x55; }
void NETBUF_$GETVA(uint32_t pa, uint32_t *va, status_$t *st)
{
    trace[nt++] = 'G';
    getva_pa = pa;
    *va = 0xD00000;
    *st = getva_status;
}
uint32_t NETBUF_$RTNVA(uint32_t *va) { trace[nt++] = 'R'; (void)va; return 0x48C00; }
void MMAP_$FREE(uint32_t vpn) { trace[nt++] = 'F'; freed = vpn; }
void ML_$LOCK(int16_t id) { trace[nt++] = (id == 0x18) ? 'L' : '?'; }
void ML_$UNLOCK(int16_t id) { trace[nt++] = (id == 0x18) ? 'U' : '?'; }
void CRASH_SYSTEM(const status_$t *s) { (void)s; trace[nt++] = 'X'; }

#include "../gethdr.c"
#include "../rtnhdr.c"

static void reset(void)
{
    memset(trace, 0, sizeof(trace));
    nt = 0;
    getva_status = 0;
    NETWORK_$LOOPBACK_FLAG = 0;
    NETWORK_$HDR_PAGE = 0xE00000;
    NETWORK_$HDR_PAGE_PA = 0x4400;
    NODE_$ME = 0x1234;
}

TEST(local_node_private_page)
{
    uint32_t node = 0x1234, va = 0, pa = 0;
    reset();
    NETWORK_$GETHDR(&node, &va, &pa);
    ASSERT_EQ(0, strcmp(trace, "CG"));
    ASSERT_EQ(0x123u << 10, pa);
    ASSERT_EQ(0x123u << 10, getva_pa);
    ASSERT_EQ(0xD00000, va);
}

TEST(loopback_private_page_crash)
{
    uint32_t node = 0x999, va = 0, pa = 0;
    reset();
    NETWORK_$LOOPBACK_FLAG = -1;
    getva_status = 0x77;
    NETWORK_$GETHDR(&node, &va, &pa);
    ASSERT_EQ(0, strcmp(trace, "CGX"));
}

TEST(remote_shared_page)
{
    uint32_t node = 0x999, va = 0, pa = 0;
    reset();
    NETWORK_$GETHDR(&node, &va, &pa);
    ASSERT_EQ(0, strcmp(trace, "L"));
    ASSERT_EQ(0xE00000, va);
    ASSERT_EQ(0x4400, pa);
}

TEST(remote_nil_page_unlocks)
{
    uint32_t node = 0x999, va = 1, pa = 0;
    reset();
    NETWORK_$HDR_PAGE = 0;
    NETWORK_$GETHDR(&node, &va, &pa);
    ASSERT_EQ(0, strcmp(trace, "LU"));
    ASSERT_EQ(0, va);
}

TEST(rtnhdr_both_arms)
{
    uint32_t va = 0xE00000;
    reset();
    NETWORK_$RTNHDR(&va);
    ASSERT_EQ(0, strcmp(trace, "U"));
    reset();
    va = 0xD00000;
    NETWORK_$RTNHDR(&va);
    ASSERT_EQ(0, strcmp(trace, "RF"));
    ASSERT_EQ(0x48C00u >> 10, freed);
}

int main(void)
{
    printf("NETWORK_$GETHDR / NETWORK_$RTNHDR tests:\n");
    RUN_TEST(local_node_private_page);
    RUN_TEST(loopback_private_page_crash);
    RUN_TEST(remote_shared_page);
    RUN_TEST(remote_nil_page_unlocks);
    RUN_TEST(rtnhdr_both_arms);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
