/*
 * rem_name/test/test_read_dir_rep.c - unit tests for REM_NAME_$READ_DIR
 * (0x00E4A984) and REM_NAME_$READ_REP (0x00E4AB44).  NETBUF_$GET_HDR /
 * NETBUF_$RTN_HDR, rem_name_$send_request and OS_$DATA_COPY are mocked; the
 * reply page is a static buffer registered in the host VA arena.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  %-52s ", #name);                  \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    unsigned long long _e = (unsigned long long)(expected);              \
    unsigned long long _a = (unsigned long long)(actual);                \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",   \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "rem_name/rem_name_internal.h"

rem_name_data_t rem_name_$data;

/* the NETBUF page, addressed through the host VA arena */
static uint8_t page[0x200] __attribute__((aligned(4)));
static int get_hdr_calls, rtn_hdr_calls;
static uint32_t rtn_va_seen;

void NETBUF_$GET_HDR(uint32_t *phys_out, uint32_t *va_out)
{
    get_hdr_calls++;
    *phys_out = 0x5000;
    *va_out = ARCH_PTR_TO_VA(page);
}

void NETBUF_$RTN_HDR(uint32_t *va_ptr)
{
    rtn_hdr_calls++;
    rtn_va_seen = *va_ptr;
}

void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{
    memcpy(dst, src, len);
}

/* ---- rem_name_$send_request mock ---- */
static int       sr_calls;
static boolean   sr_result;
static status_$t sr_status;
static uint8_t   sr_req_seen[0x36];
static int16_t   sr_size_seen, sr_opcode_seen, sr_resp_size_seen;
static void     *sr_resp_seen;

boolean rem_name_$send_request(uint32_t net, uint32_t node, void *request,
                               int16_t req_size, int16_t flags, int16_t opcode,
                               void *response, int16_t resp_size,
                               int16_t *resp_len_ret, status_$t *status_ret)
{
    (void)net; (void)node; (void)flags;
    sr_calls++;
    memcpy(sr_req_seen, request, sizeof(sr_req_seen));
    sr_size_seen = req_size; sr_opcode_seen = opcode; sr_resp_size_seen = resp_size;
    sr_resp_seen = response;
    *resp_len_ret = 0x100;
    *status_ret = sr_status;
    return sr_result;
}

#include "../read_dir.c"
#include "../read_rep.c"

static uid_t dir_uid = { 0x11111111, 0x22222222 };

static void reset(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)page - 0x10000;
    memset(page, 0, sizeof(page));
    get_hdr_calls = rtn_hdr_calls = 0;
    sr_calls = 0;
    sr_result = (boolean)-1;
    sr_status = status_$ok;
}

/* append one wire entry: type, name_len, name, then uid+extra for type 1 */
static uint8_t *wire_put(uint8_t *p, int16_t type, const char *name, uint8_t tag)
{
    uint16_t len = (uint16_t)strlen(name);
    int i;
    memcpy(p, &type, 2); p += 2;
    memcpy(p, &len, 2);  p += 2;
    memcpy(p, name, len); p += len;
    if (type == 1) {
        for (i = 0; i < 12; i++) p[i] = (uint8_t)(tag + i);
        p += 12;
    }
    return p;
}

static void set_count(uint16_t n)
{
    memcpy(page + 0x16, &n, 2);
}

/* ---------------- READ_DIR ---------------- */

TEST(read_dir_request_and_unpack)
{
    rem_name_$dir_entry_t out[4];
    uint16_t count = 9;
    status_$t st = 0x55;
    uint8_t *p;
    uint32_t v; uint16_t w;

    reset();
    p = page + 0x18;
    p = wire_put(p, 1, "alpha", 0x10);
    p = wire_put(p, 2, "lnk", 0);
    p = wire_put(p, 1, "abcdefghijklmnopqrstuvwxyz012345", 0x40);   /* 32 chars */
    set_count(3);
    memset(out, 0xEE, sizeof(out));

    REM_NAME_$READ_DIR(1, 2, &dir_uid, 0x1234, out, 4, &count, &st);

    ASSERT_EQ(1, get_hdr_calls);
    ASSERT_EQ(1, rtn_hdr_calls);
    ASSERT_EQ(ARCH_PTR_TO_VA(page), rtn_va_seen);
    ASSERT_EQ(1, sr_calls);
    memcpy(&v, sr_req_seen, 4);        ASSERT_EQ(REM_NAME_OP_READ_DIR, v);
    memcpy(&w, sr_req_seen + 0xC, 2);  ASSERT_EQ(1, w);
    memcpy(&v, sr_req_seen + 0x32, 4); ASSERT_EQ(0x1234, v);
    ASSERT_EQ(0x36, sr_size_seen);
    ASSERT_EQ(0x0C, sr_opcode_seen);
    ASSERT_EQ(0x200, sr_resp_size_seen);
    ASSERT_EQ((uintptr_t)page, (uintptr_t)sr_resp_seen);

    ASSERT_EQ(3, count);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, out[0].type);
    ASSERT_EQ(5, out[0].name_len);
    ASSERT_EQ(0, memcmp(out[0].name, "alpha                           ", 32));
    ASSERT_EQ(0x10, ((uint8_t *)&out[0].uid)[0]);
    ASSERT_EQ(0x1B, ((uint8_t *)&out[0].extra)[3]);
    ASSERT_EQ(3, out[1].type);
    ASSERT_EQ(3, out[1].name_len);
    ASSERT_EQ(0, memcmp(out[1].name, "lnk                             ", 32));
    ASSERT_EQ(0xEE, ((uint8_t *)&out[1].uid)[0]);       /* link: uid untouched */
    ASSERT_EQ(1, out[2].type);
    ASSERT_EQ(32, out[2].name_len);
    ASSERT_EQ(0, memcmp(out[2].name, "abcdefghijklmnopqrstuvwxyz012345", 32));
    ASSERT_EQ(0x40, ((uint8_t *)&out[2].uid)[0]);
    ASSERT_EQ(0xEEEE, (uint16_t)out[3].type);
}

/* `cmp.w (A2),D4w / bls`: stop when max is reached */
TEST(read_dir_honours_max)
{
    rem_name_$dir_entry_t out[4];
    uint16_t count = 9;
    status_$t st = 0x55;
    uint8_t *p;

    reset();
    p = page + 0x18;
    p = wire_put(p, 1, "a", 0);
    p = wire_put(p, 1, "b", 0);
    p = wire_put(p, 1, "c", 0);
    set_count(3);
    memset(out, 0xEE, sizeof(out));
    REM_NAME_$READ_DIR(1, 2, &dir_uid, 1, out, 2, &count, &st);
    ASSERT_EQ(2, count);
    ASSERT_EQ('b', out[1].name[0]);
    ASSERT_EQ(0xEEEE, (uint16_t)out[2].type);
    ASSERT_EQ(1, rtn_hdr_calls);

    /* max 0: nothing at all */
    reset();
    p = page + 0x18;
    wire_put(p, 1, "a", 0);
    set_count(1);
    REM_NAME_$READ_DIR(1, 2, &dir_uid, 1, out, 0, &count, &st);
    ASSERT_EQ(0, count);
}

/* 0x00E4AB28: an unknown wire type gives the record back and stops */
TEST(read_dir_unknown_type_backs_out)
{
    rem_name_$dir_entry_t out[4];
    uint16_t count = 9;
    status_$t st = 0x55;
    uint8_t *p;

    reset();
    p = page + 0x18;
    p = wire_put(p, 1, "ok", 0x20);
    p = wire_put(p, 9, "bad", 0);
    p = wire_put(p, 1, "never", 0);
    set_count(3);
    memset(out, 0xEE, sizeof(out));
    REM_NAME_$READ_DIR(1, 2, &dir_uid, 1, out, 4, &count, &st);
    ASSERT_EQ(1, count);
    ASSERT_EQ(3, out[1].name_len);              /* name_len/name were stored */
    ASSERT_EQ(0xEEEE, (uint16_t)out[1].type);   /* but not the type */
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, rtn_hdr_calls);
}

/* send failure: only 0xE001A carries on, cleared iff max < reply count */
TEST(read_dir_send_failure_paths)
{
    rem_name_$dir_entry_t out[4];
    uint16_t count = 9;
    status_$t st = 0x55;
    uint8_t *p;

    reset();
    sr_result = 0; sr_status = 0x000E0033;
    set_count(1);
    wire_put(page + 0x18, 1, "a", 0);
    REM_NAME_$READ_DIR(1, 2, &dir_uid, 1, out, 4, &count, &st);
    ASSERT_EQ(0, count);
    ASSERT_EQ(0x000E0033, st);
    ASSERT_EQ(1, rtn_hdr_calls);

    /* 0xE001A with max (4) >= count (2): status kept, entries unpacked */
    reset();
    sr_result = 0; sr_status = status_$naming_last_entry_in_replicated_root_returned;
    p = page + 0x18;
    p = wire_put(p, 1, "a", 0);
    p = wire_put(p, 1, "b", 0);
    set_count(2);
    REM_NAME_$READ_DIR(1, 2, &dir_uid, 1, out, 4, &count, &st);
    ASSERT_EQ(2, count);
    ASSERT_EQ(status_$naming_last_entry_in_replicated_root_returned, st);

    /* 0xE001A with max (1) < count (2): status cleared */
    reset();
    sr_result = 0; sr_status = status_$naming_last_entry_in_replicated_root_returned;
    p = page + 0x18;
    p = wire_put(p, 1, "a", 0);
    p = wire_put(p, 1, "b", 0);
    set_count(2);
    REM_NAME_$READ_DIR(1, 2, &dir_uid, 1, out, 1, &count, &st);
    ASSERT_EQ(1, count);
    ASSERT_EQ(status_$ok, st);

    /* empty reply */
    reset();
    set_count(0);
    REM_NAME_$READ_DIR(1, 2, &dir_uid, 1, out, 4, &count, &st);
    ASSERT_EQ(0, count);
    ASSERT_EQ(1, rtn_hdr_calls);
}

/* ---------------- READ_REP ---------------- */

TEST(read_rep_copies_records)
{
    rem_name_$rep_entry_t out[3];
    uint16_t count = 9;
    status_$t st = 0x55;
    int i;
    uint32_t v;

    reset();
    for (i = 0; i < 0x12 * 3; i++) page[0x18 + i] = (uint8_t)(0x40 + i);
    set_count(3);
    memset(out, 0xEE, sizeof(out));
    REM_NAME_$READ_REP(1, 2, &dir_uid, 0x77, out, 2, &count, &st);

    memcpy(&v, sr_req_seen, 4);        ASSERT_EQ(REM_NAME_OP_READ_REP, v);
    memcpy(&v, sr_req_seen + 0x32, 4); ASSERT_EQ(0x77, v);
    ASSERT_EQ(0x0E, sr_opcode_seen);
    ASSERT_EQ(0x36, sr_size_seen);
    ASSERT_EQ(2, count);                        /* max 2 of 3 */
    ASSERT_EQ(0, memcmp(&out[0], page + 0x18, 0x12));
    ASSERT_EQ(0, memcmp(&out[1], page + 0x18 + 0x12, 0x12));
    ASSERT_EQ(0xEE, ((uint8_t *)&out[2])[0]);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, rtn_hdr_calls);
}

TEST(read_rep_failure_paths)
{
    rem_name_$rep_entry_t out[3];
    uint16_t count = 9;
    status_$t st = 0x55;

    reset();
    sr_result = 0; sr_status = 0x000E0033;
    set_count(1);
    REM_NAME_$READ_REP(1, 2, &dir_uid, 0, out, 3, &count, &st);
    ASSERT_EQ(0, count);
    ASSERT_EQ(0x000E0033, st);
    ASSERT_EQ(1, rtn_hdr_calls);

    /* 0xE001A: records still copied, status NOT cleared (no max test here) */
    reset();
    sr_result = 0; sr_status = status_$naming_last_entry_in_replicated_root_returned;
    set_count(1);
    REM_NAME_$READ_REP(1, 2, &dir_uid, 0, out, 0, &count, &st);
    ASSERT_EQ(0, count);
    ASSERT_EQ(status_$naming_last_entry_in_replicated_root_returned, st);

    reset();
    sr_result = 0; sr_status = status_$naming_last_entry_in_replicated_root_returned;
    set_count(1);
    REM_NAME_$READ_REP(1, 2, &dir_uid, 0, out, 3, &count, &st);
    ASSERT_EQ(1, count);
    ASSERT_EQ(status_$naming_last_entry_in_replicated_root_returned, st);
}

int main(void)
{
    printf("REM_NAME_$READ_DIR / READ_REP tests\n");
    RUN_TEST(read_dir_request_and_unpack);
    RUN_TEST(read_dir_honours_max);
    RUN_TEST(read_dir_unknown_type_backs_out);
    RUN_TEST(read_dir_send_failure_paths);
    RUN_TEST(read_rep_copies_records);
    RUN_TEST(read_rep_failure_paths);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
