/*
 * network/test/test_init.c - unit tests for NETWORK_$INIT (0x00E2F684)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>

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

#include "network/network_internal.h"
#include "io/io.h"
#include "netlog/netlog.h"
#include "route/route.h"
#include "time/time.h"

network_table_entry_t NETWORK_$NET_TABLE[NETWORK_TABLE_SIZE];
uint32_t NETWORK_$RQST_WAIT[4];
uint32_t NETWORK_$PAGE_WAIT[4];
uint32_t NETWORK_$RING_DCTE;
uint16_t NETWORK_$AGE_TICKS;
uint32_t NETWORK_$HDR_PAGE_PA;
uint32_t NETWORK_$HDR_PAGE;
route_$port_t ROUTE_$PORT_ARRAY[ROUTE_$MAX_PORTS];
ec_$eventcount_t NETLOG_$EC;
uint32_t TIME_$CLOCKH;

static char log_buf[128];
static void note(const char *s) { strcat(log_buf, s); }
static int16_t svc_op[2];
static uint32_t svc_val[2];
static int n_svc;
static uint16_t dcte_type, dcte_num;
static status_$t dcte_status, getva_status, add_status, crash_status;
static uint32_t getva_pa;
static int16_t add_count;
static int n_crash;
static jmp_buf crash_jmp;
static uint8_t dcte_obj[16];

void PKT_$INIT(void) { note("K"); }
void NETBUF_$INIT(void) { note("B"); }
void MSG_$INIT(void) { note("M"); }
void NET_IO_$INIT(void) { note("I"); }
void RIP_$INIT(void) { note("R"); }
void MAC_OS_$INIT(void) { note("A"); }
void XNS_IDP_$INIT(void) { note("X"); }
void NETWORK_$SET_SERVICE(int16_t *op_ptr, uint32_t *value_ptr, status_$t *st)
{
    svc_op[n_svc] = *op_ptr; svc_val[n_svc] = *value_ptr; n_svc++; note("S"); *st = 0;
}
void WP_$CALLOC(uint32_t *ppn_out, status_$t *status) { *ppn_out = 0x234; *status = 0x55; note("W"); }
void NETBUF_$GETVA(uint32_t pa, uint32_t *va_out, status_$t *status)
{
    getva_pa = pa; *va_out = 0xD00000; *status = getva_status; note("V");
}
dcte_t *IO_$GET_DCTE(uint16_t *ctypep, uint16_t *cnump, status_$t *status_ret)
{
    dcte_type = *ctypep; dcte_num = *cnump; *status_ret = dcte_status; note("D");
    return (dcte_t *)(void *)dcte_obj;
}
int16_t NETWORK_$ADD_PAGE_SERVERS(int16_t *count_ptr, status_$t *status_ret)
{
    add_count = *count_ptr; *status_ret = add_status; note("P"); return 0;
}
void CRASH_SYSTEM(const status_$t *s) { n_crash++; crash_status = *s; longjmp(crash_jmp, 1); }

#include "../init.c"

static void reset_state(void)
{
    int i;
    for (i = 0; i < NETWORK_TABLE_SIZE; i++) {
        NETWORK_$NET_TABLE[i].refcount = 0xAAAA;
        NETWORK_$NET_TABLE[i].net_id = 0xBBBB;
    }
    memset(ROUTE_$PORT_ARRAY, 0, sizeof(ROUTE_$PORT_ARRAY));
    log_buf[0] = 0;
    n_svc = n_crash = 0;
    dcte_status = getva_status = add_status = 0;
    NETLOG_$EC.value = 0x40;
    TIME_$CLOCKH = 0x1000;
    NETWORK_$AGE_TICKS = 7;
    ARCH_HOST_VA_BASE = (uintptr_t)dcte_obj - 0x9000u;
}

static void test_normal(void)
{
    NETWORK_$INIT();
    ASSERT_EQ(0, strcmp(log_buf, "KSSBMWVIRAXDP"));
    ASSERT_EQ(0xAAAA, NETWORK_$NET_TABLE[0].refcount);     /* slot 0 untouched */
    ASSERT_EQ(0, NETWORK_$NET_TABLE[1].refcount);
    ASSERT_EQ(0, NETWORK_$NET_TABLE[64].net_id);
    ASSERT_EQ(2, svc_op[0]);
    ASSERT_EQ(0, svc_val[0]);
    ASSERT_EQ(3, svc_op[1]);
    ASSERT_EQ(0, svc_val[1]);
    ASSERT_EQ(0x234u << 10, NETWORK_$HDR_PAGE_PA);
    ASSERT_EQ(0x234u << 10, getva_pa);
    ASSERT_EQ(0xD00000, NETWORK_$HDR_PAGE);
    ASSERT_EQ(2, dcte_type);
    ASSERT_EQ(0, dcte_num);
    ASSERT_EQ(0x9000, NETWORK_$RING_DCTE);
    ASSERT_EQ(1, NETWORK_$PAGE_WAIT[0]);
    ASSERT_EQ(0, NETWORK_$PAGE_WAIT[1]);
    ASSERT_EQ(0x41, NETWORK_$PAGE_WAIT[2]);
    ASSERT_EQ(0x1004, NETWORK_$PAGE_WAIT[3]);
    ASSERT_EQ(1, add_count);
    ASSERT_EQ(1, NETWORK_$RQST_WAIT[2]);
    ASSERT_EQ(0x103C, NETWORK_$RQST_WAIT[3]);
    ASSERT_EQ(0, NETWORK_$AGE_TICKS);
}

static void test_routing_port_and_no_ring(void)
{
    ROUTE_$PORT_ARRAY[0].active = 3;
    dcte_status = 0x00100001;
    NETWORK_$INIT();
    ASSERT_EQ(0x00170000, svc_val[0]);
    ASSERT_EQ(0x00170000, svc_val[1]);  /* flags kept, pool word cleared */
    ASSERT_EQ(0, NETWORK_$RING_DCTE);
}

static void test_getva_failure_crashes(void)
{
    getva_status = 0x00110003;
    if (setjmp(crash_jmp) == 0) {
        NETWORK_$INIT();
    }
    ASSERT_EQ(1, n_crash);
    ASSERT_EQ(0x00110003, crash_status);
    ASSERT_EQ(0, strcmp(log_buf, "KSSBMWV"));
}

static void test_add_page_servers_failure_crashes(void)
{
    add_status = 0x00110002;
    if (setjmp(crash_jmp) == 0) {
        NETWORK_$INIT();
    }
    ASSERT_EQ(1, n_crash);
    ASSERT_EQ(0x00110002, crash_status);
}

int main(void)
{
    printf("NETWORK_$INIT tests\n");
    RUN_TEST(normal);
    RUN_TEST(routing_port_and_no_ring);
    RUN_TEST(getva_failure_crashes);
    RUN_TEST(add_page_servers_failure_crashes);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
