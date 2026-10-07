/*
 * area/test/test_rpmap_get.c - unit tests for area_$rpmap_get (0x00E07370).
 * The RPMAP cache window lives in a host arena reached through
 * ARCH_HOST_VA_BASE.
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

#include "area/area_internal.h"

/* AREA_RPMAP_CACHE_VA and AREA_PIT_PAGES_VA are ARCH_PTR_TO_VA of the
 * sau2.ld symbols AREA_$RPMAP_CACHE and PIT_PAGES, past OS_PAGE_END since
 * source-o7s2 (docs/rfc-cold-start.md section 8c); this test stands in for
 * the link with the map's values (os/test/test_vm_tables.c checks the
 * macros themselves). */
#undef AREA_RPMAP_CACHE_VA
#define AREA_RPMAP_CACHE_VA 0x00EE4C00u
#undef AREA_PIT_PAGES_VA
#define AREA_PIT_PAGES_VA 0x00EE6400u
#include "anon/anon.h"
#include "ast/ast.h"
#include "ec/ec.h"
#include "network/network.h"
#include "misc/crash_system.h"

area_$globals_t AREA_$GLOBALS;
uid_t ANON_$UID = { 0xA0A0A0A0, 0x0B0B0B0B };

static uint8_t arena[3 * 0x400];
static char log_buf[128];
static void note(const char *s) { strcat(log_buf, s); }
static status_$t write_status, fetch_status;
static network_$page_request_t w_req, f_req;
static uint32_t w_ppn;
static uint16_t w_pkt, f_pkt, w_count;
static void *f_buf;
static status_$t f_status_arg;
static int32_t wait_seen;
static int wait_clears;
static int crashes;

void ML_$LOCK(int16_t id) { if (id != 0x12) note("!"); note("L"); }
void ML_$UNLOCK(int16_t id) { if (id != 0x12) note("!"); note("U"); }
void NETWORK_$WRITE(void *node_addr, network_$page_request_t *req,
                    uint32_t ppn, uint16_t pkt_size, uint16_t count,
                    uint16_t *dtv_low, clock_t *dtm, status_$t *status)
{
    (void)dtv_low; (void)dtm;
    note("W");
    if (node_addr != &AREA_$PARTNER) note("!");
    w_req = *req; w_ppn = ppn; w_pkt = pkt_size; w_count = count;
    *status = write_status;
}
void AST_$FETCH_PMAP_PAGE(void *uid_info, uint32_t *output_buf, uint16_t flags,
                          status_$t status)
{
    note("F");
    f_req = *(network_$page_request_t *)uid_info;
    f_buf = output_buf; f_pkt = flags; f_status_arg = status;
    output_buf[0] = 0xFEEDu;
}
uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *wait_val, int16_t num_ecs)
{
    note("E");
    if (ecs[0] != &AREA_$GLOBALS.rpmap_in_trans_ec || num_ecs != 1) note("!");
    wait_seen = *wait_val;
    AREA_$GLOBALS.rpmap_cache[wait_clears].in_trans = 0;
    return 1;
}
void EC_$ADVANCE(ec_$eventcount_t *ec)
{
    note("A");
    if (ec != &AREA_$GLOBALS.rpmap_in_trans_ec) note("!");
}
void CRASH_SYSTEM(const status_$t *status_p) { (void)status_p; crashes++; note("C"); }

#include "../rpmap_get.c"

static area_$entry_t entry;
static status_$t st;

static void reset_state(void)
{
    int i;
    memset(&AREA_$GLOBALS, 0, sizeof(AREA_$GLOBALS));
    memset(arena, 0, sizeof(arena));
    ARCH_HOST_VA_BASE = (uintptr_t)arena - AREA_RPMAP_CACHE_VA;
    for (i = 0; i < 3; i++) {
        AREA_$GLOBALS.rpmap_cache[i].group = 0xFFFF;
        AREA_$GLOBALS.rpmap_page[i] = 0x100 + i;
    }
    AREA_$GLOBALS.partner_pkt_size = 0x480;
    AREA_$GLOBALS.rpmap_in_trans_ec.value = 41;
    memset(&entry, 0, sizeof(entry));
    entry.remote_volx = 5;
    log_buf[0] = 0;
    write_status = fetch_status = 0;
    wait_clears = 0;
    crashes = 0;
    st = 0x1234;
}

static void test_miss_reads_into_lru_slot(void)
{
    uint32_t *p;
    AREA_$GLOBALS.rpmap_cache[0].seq = 9;
    AREA_$GLOBALS.rpmap_cache[1].seq = 3;
    AREA_$GLOBALS.rpmap_cache[2].seq = 7;
    AREA_$GLOBALS.rpmap_seq = 20;
    p = area_$rpmap_get(&entry, 0x2B, 0, 0, &st);
    ASSERT_EQ(0, strcmp(log_buf, "UFLA"));
    ASSERT_EQ((unsigned long)(arena + 0x400), (unsigned long)p);
    ASSERT_EQ((unsigned long)(arena + 0x400), (unsigned long)f_buf);
    ASSERT_EQ(0xFEED, p[0]);
    ASSERT_EQ(0xA0A0A0A0u, f_req.uid.high);
    ASSERT_EQ(5, f_req.uid.low);
    ASSERT_EQ(5 * 0x101, f_req.page_num);
    ASSERT_EQ(1, ((uint8_t *)&f_req)[0x10]);
    ASSERT_EQ(0, ((uint8_t *)&f_req)[0x11]);
    ASSERT_EQ(0x480, f_pkt);
    ASSERT_EQ(0, f_status_arg);
    ASSERT_EQ(5, AREA_$GLOBALS.rpmap_cache[1].volx);
    ASSERT_EQ(5, AREA_$GLOBALS.rpmap_cache[1].group);
    ASSERT_EQ(0, AREA_$GLOBALS.rpmap_cache[1].in_trans);
    ASSERT_EQ(21, AREA_$GLOBALS.rpmap_cache[1].seq);
    ASSERT_EQ(21, AREA_$GLOBALS.rpmap_seq);
    ASSERT_EQ(0, st);
}

static void test_hit_marks_dirty(void)
{
    uint32_t *p;
    AREA_$GLOBALS.rpmap_cache[2].volx = 5;
    AREA_$GLOBALS.rpmap_cache[2].group = 1;
    p = area_$rpmap_get(&entry, 0x0F, (int8_t)0xFF, 0, &st);
    ASSERT_EQ(0, strlen(log_buf));
    ASSERT_EQ((unsigned long)(arena + 0x800), (unsigned long)p);
    ASSERT_EQ(0xFF, (uint8_t)AREA_$GLOBALS.rpmap_cache[2].dirty);
    ASSERT_EQ(1, AREA_$GLOBALS.rpmap_cache[2].seq);
}

static void test_flag_claims_without_read(void)
{
    uint32_t *p = area_$rpmap_get(&entry, 0x10, 0, (int8_t)0xFF, &st);
    ASSERT_EQ(0, strlen(log_buf));
    ASSERT_EQ((unsigned long)arena, (unsigned long)p);
    ASSERT_EQ(2, AREA_$GLOBALS.rpmap_cache[0].group);
}

static void test_dirty_victim_written_back(void)
{
    int i;
    for (i = 0; i < 3; i++) AREA_$GLOBALS.rpmap_cache[i].seq = 10 + i;
    AREA_$GLOBALS.rpmap_cache[0].volx = 7;
    AREA_$GLOBALS.rpmap_cache[0].group = 0xFFFE;    /* sign-extended */
    AREA_$GLOBALS.rpmap_cache[0].dirty = (int8_t)0xFF;
    area_$rpmap_get(&entry, 0, 0, 0, &st);
    ASSERT_EQ(0, strcmp(log_buf, "UWLAUFLA"));
    ASSERT_EQ(7, w_req.uid.low);
    ASSERT_EQ((uint32_t)(-2 * 0x101), w_req.page_num);
    ASSERT_EQ(0x100, w_ppn);
    ASSERT_EQ(1, w_count);
    ASSERT_EQ(0, AREA_$GLOBALS.rpmap_cache[0].dirty);
    ASSERT_EQ(0, crashes);
}

static void test_write_error_crashes(void)
{
    AREA_$GLOBALS.rpmap_cache[1].seq = 1;
    AREA_$GLOBALS.rpmap_cache[2].seq = 1;
    AREA_$GLOBALS.rpmap_cache[0].dirty = (int8_t)0xFF;
    write_status = 0x00110004;
    area_$rpmap_get(&entry, 0, 0, (int8_t)0xFF, &st);
    ASSERT_EQ(1, crashes);
}

static void test_in_transition_waits(void)
{
    AREA_$GLOBALS.rpmap_cache[0].volx = 5;
    AREA_$GLOBALS.rpmap_cache[0].group = 0;
    AREA_$GLOBALS.rpmap_cache[0].in_trans = (int8_t)0xFF;
    area_$rpmap_get(&entry, 3, 0, 0, &st);
    ASSERT_EQ(0, strcmp(log_buf, "UEL"));
    ASSERT_EQ(42, wait_seen);
}

int main(void)
{
    printf("area_$rpmap_get tests\n");
    RUN_TEST(miss_reads_into_lru_slot);
    RUN_TEST(hit_marks_dirty);
    RUN_TEST(flag_claims_without_read);
    RUN_TEST(dirty_victim_written_back);
    RUN_TEST(write_error_crashes);
    RUN_TEST(in_transition_waits);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
