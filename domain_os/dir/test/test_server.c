/*
 * dir/test/test_server.c - unit tests for DIR_$SERVER (0x00E58200)
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

#include "dir/dir_internal.h"
#include "acl/acl.h"
#include "audit/audit.h"
#include "uid/uid.h"

MODULE_DATA_DEFINE(dir_$data_t, DIR_$DATA, 0x00E7DBF8);
MODULE_DATA_DEFINE(dir_$server_data_t, DIR_SERVER, 0x00E801E4);
uid_t UID_$NIL;

/* ---- call log ---- */
static char log_buf[256];
static void note(const char *s) { strcat(log_buf, s); }
static int n_get, n_set;
static const void *set_sids[4], *set_proj[4];
static status_$t set_status_val[4], proj_status_val;
static int16_t last_proj_count;
static const void *last_proj_list;
static uint16_t do_op_req_size, do_op_resp_size;

void ACL_$GET_RE_ALL_SIDS(void *acl_data, void *re_sids, void *prot_info,
                          void *subsys_ids, status_$t *status)
{
    (void)acl_data; (void)prot_info; (void)subsys_ids;
    ((uint32_t *)re_sids)[0] = 0x5E5E; n_get++; note("G"); *status = 0;
}
void ACL_$SET_RE_ALL_SIDS(void *o, void *c, void *sp, void *cp, status_$t *st)
{
    (void)o; (void)sp;
    set_sids[n_set] = c; set_proj[n_set] = cp; *st = set_status_val[n_set];
    n_set++; note("S");
}
void ACL_$SET_PROJ_LIST(uid_t *proj_acls, int16_t *count, status_$t *st)
{
    last_proj_list = proj_acls; last_proj_count = *count; note("P");
    *st = (proj_acls == &UID_$NIL) ? 0 : proj_status_val;
}
void ACL_$ENTER_SUPER(void) { note("E"); }
void ACL_$EXIT_SUPER(void)  { note("X"); }
void ACL_$UP(void)          { note("U"); }
void ACL_$DOWN(void)        { note("D"); }
void AUDIT_$SUSPEND(void)   { note("s"); }
void AUDIT_$RESUME(void)    { note("r"); }
void DIR_$DO_OP(void *request, int16_t req_size, int16_t resp_size,
                void *response, uint16_t *received_len)
{
    (void)request; (void)response;
    do_op_req_size = (uint16_t)req_size; do_op_resp_size = (uint16_t)resp_size;
    *received_len = 0x30; note("O");
}

#include "../server.c"

static union { dir_$server_request_t r; uint8_t raw[0x292]; } rq;
static union { dir_$server_reply_t r; uint8_t raw[0x11A]; } rp;
static uint16_t rlen;

static void reset_state(void)
{
    memset(&rq, 0, sizeof(rq));
    memset(&rp, 0xAA, sizeof(rp));
    memset(&DIR_$DATA, 0, sizeof(DIR_$DATA));
    memset(&DIR_SERVER, 0, sizeof(DIR_SERVER));
    DIR_SERVER.default_proj[0] = DIR_SERVER.default_proj[1] =
        DIR_SERVER.default_proj[2] = 0x0C;
    DIR_SERVER.first_time = (int8_t)0xFF;
    log_buf[0] = 0;
    n_get = n_set = 0;
    memset(set_status_val, 0, sizeof(set_status_val));
    proj_status_val = 0;
    rlen = 0;
    rq.r.op = 0x40;
    rq.r.hdr_version = 1;
    rq.r.version = 1;
    DIR_$OP_REC(0x40 >> 1).version = 2;
    rq.r.proj_count = 3;
}

static void test_header_version_too_new(void)
{
    rq.r.hdr_version = 2;
    DIR_$SERVER(&rq, &rp, &rlen);
    ASSERT_EQ(0x14, rlen);
    ASSERT_EQ(0x000E0026, rp.r.status);
    ASSERT_EQ(1, rp.r.version);
    ASSERT_EQ(0, rp.r.long_08);
    ASSERT_EQ(0, rp.r.word_12);
    ASSERT_EQ(0, strlen(log_buf));
}

static void test_body_version_too_new(void)
{
    rq.r.version = 3;
    DIR_$SERVER(&rq, &rp, &rlen);
    ASSERT_EQ(0x000E0028, rp.r.status);
    ASSERT_EQ(2, rp.r.version);
    ASSERT_EQ(0, strlen(log_buf));
}

static void test_normal_request(void)
{
    DIR_$SERVER(&rq, &rp, &rlen);
    ASSERT_EQ(0, strcmp(log_buf, "GEsSPrXOEsSPrX"));
    ASSERT_EQ(0, DIR_SERVER.first_time);
    ASSERT_EQ(0x5E5E, DIR_SERVER.saved_re_sids[0]);
    ASSERT_EQ((uintptr_t)rq.r.re_sids, (uintptr_t)set_sids[0]);
    ASSERT_EQ((uintptr_t)rq.r.cur_proj, (uintptr_t)set_proj[0]);
    ASSERT_EQ((uintptr_t)DIR_SERVER.saved_re_sids, (uintptr_t)set_sids[1]);
    ASSERT_EQ((uintptr_t)DIR_SERVER.saved_subsys, (uintptr_t)set_proj[1]);
    ASSERT_EQ(0x292, do_op_req_size);
    ASSERT_EQ(0x11A, do_op_resp_size);
    ASSERT_EQ(0x30, rlen);
    ASSERT_EQ((uintptr_t)&UID_$NIL, (uintptr_t)last_proj_list);
    ASSERT_EQ(0, last_proj_count);
    ASSERT_EQ(0, rq.r.cur_proj[0]);     /* header version 1: kept */
}

static void test_second_request_no_save_and_flags(void)
{
    DIR_SERVER.first_time = 0;
    rq.r.hdr_version = 0;
    rq.r.flags = DIR_SERVER_REQ_KEEP_AUDIT | DIR_SERVER_REQ_SUBSYS_UP;
    DIR_$SERVER(&rq, &rp, &rlen);
    ASSERT_EQ(0, n_get);
    ASSERT_EQ(0, strcmp(log_buf, "EsSPUXOEDSPrX"));
    ASSERT_EQ(0x0C, rq.r.cur_proj[2]);  /* default project record */
}

static void test_set_sids_failure_skips_op(void)
{
    set_status_val[0] = 0x00230001;
    DIR_$SERVER(&rq, &rp, &rlen);
    ASSERT_EQ(0x00230001, rp.r.status);
    ASSERT_EQ(0, strcmp(log_buf, "GEsSSPrX"));
    ASSERT_EQ(0x14, rlen);
}

static void test_proj_failure_skips_op(void)
{
    proj_status_val = 0x00230002;
    DIR_$SERVER(&rq, &rp, &rlen);
    ASSERT_EQ(0x00230002, rp.r.status);
    ASSERT_EQ(0, strcmp(log_buf, "GEsSPSPrX"));
}

int main(void)
{
    printf("DIR_$SERVER tests\n");
    RUN_TEST(header_version_too_new);
    RUN_TEST(body_version_too_new);
    RUN_TEST(normal_request);
    RUN_TEST(second_request_no_save_and_flags);
    RUN_TEST(set_sids_failure_skips_op);
    RUN_TEST(proj_failure_skips_op);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
