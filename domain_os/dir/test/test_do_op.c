/*
 * dir/test/test_do_op.c - tests for DIR_$DO_OP's remote path (0x00E4C02C)
 *
 * The real dir/do_op.c is #included at the bottom and driven through mocks of
 * everything it calls.  The behaviours exercised are the ones source-oby6 is
 * about:
 *   - DIR_$IS_RETRYABLE_STATUS gets the whole status longword
 *     (0x00E4C150 `move.l D1,-(SP)`), not a truncated word;
 *   - DIR_$UPDATE_HINT gets the two hint longwords BY VALUE and a real
 *     redirect longword - reply+0x30 for a RESOLVE (0x00E4C1E2) and
 *     reply+0x1E for a GET_ENTRYU (0x00E4C222), never 0;
 *   - HINT_$ADDI and DIR_$UPDATE_HINT index hints[k], the pair the request
 *     was just sent to (0x00E4C1A6 / 0x00E4C230 with D2 = D3*8);
 *   - the reply-version test at 0x00E4C174-0x00E4C18C;
 *   - the 0x000E0016 retry loop (0x00E4C134) and the stale-object fallback
 *     at 0x00E4C9BE;
 *   - dir_$do_op_resolve's fourteen arguments in the order the case-0x58
 *     block pushes them (0x00E4C894-0x00E4C8CE).
 */

#include <stdio.h>
#include <string.h>

#include "dir/dir_internal.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name)      static void test_##name(void)
#define RUN_TEST(name)  do {                                                  \
        printf("  %-52s ", #name);                                            \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        if (current_failed == 0) { printf("PASSED\n"); }                      \
    } while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
        unsigned long long _e = (unsigned long long)(expected);               \
        unsigned long long _a = (unsigned long long)(actual);                 \
        if (_e != _a) {                                                       \
            if (current_failed == 0) { printf("FAILED\n"); }                  \
            printf("      line %d: expected 0x%llx, got 0x%llx\n",            \
                   __LINE__, _e, _a);                                         \
            current_failed = 1; tests_failed++;                               \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ============================================================================
 * Globals the function reads
 * ============================================================================ */

uint32_t NODE_$ME = 0x00012345;
uint16_t PROC1_$CURRENT = 4;
#include "proc1/proc1.h"
MODULE_DATA_DEFINE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);
int8_t   AUDIT_$ENABLED = 0;            /* >= 0: auditing off */
/* 0x00E7FC66 is DIR_$OP_TAB[4].base_size and DIR_$OP_VERSION /
 * DIR_$OP_REPLY_SIZE are DIR_$OP_REC(half).reply_version / .reply_size, so
 * the one biased table is all that has to exist here (bead source-wk2f). */
dir_$op_tab_entry_t DIR_$OP_TAB[DIR_$OP_TAB_ENTRIES];
uint32_t DIR_$CONST_ZERO_L;   /* 0x00E4B33C is a longword (dir_internal.h) */
uid_t    ACL_$DIRIN_ACL = { 0x00000603u, 0x00000000u };  /* 0xE1745C */
status_$t Naming_bad_request_header_ver_err;
status_$t *PTR_Naming_bad_request_header_ver_err_00e7dbfc;

/* ============================================================================
 * Mock bookkeeping
 * ============================================================================ */

#define TEST_OP_RESOLVE     0x58
#define TEST_OP_GET_ENTRYU  0x44
#define TEST_OP_DELETE      0x30        /* any op that is neither of those */

static uint32_t  mock_hint_node[8];
static uint32_t  mock_hint_loc[8];
static int16_t   mock_hint_count;

static int       mock_rn_calls;
static uint32_t  mock_rn_hint_loc;
static uint32_t  mock_rn_hint_node;
static int16_t   mock_rn_req_size;
static status_$t mock_rn_status[8];     /* one canned reply per attempt */
static uint16_t  mock_rn_reply_ver;
static uint16_t  mock_rn_accepted_ver;
static uint32_t  mock_rn_uid_high;      /* reply+0x16 */
static uint32_t  mock_rn_uid_low;       /* reply+0x1A */
static uint16_t  mock_rn_len_word;      /* reply+0x14 */
static uint32_t  mock_rn_at_1e;         /* reply+0x1E */
static uint32_t  mock_rn_at_30;         /* reply+0x30 */

static int       mock_addi_calls;
static uint32_t  mock_addi_loc;
static uint32_t  mock_addi_node;

static int       mock_update_hint_calls;
static uint32_t  mock_update_hint_loc;
static uint32_t  mock_update_hint_node;
static uint32_t  mock_update_hint_extra;
static void     *mock_update_hint_redirect;

static int       mock_retryable_calls;
static status_$t mock_retryable_arg;
static int8_t    mock_retryable_result;

/* dir_$do_op_resolve argument capture */
static int       mock_resolve_calls;
static uint32_t  mock_resolve_path;
static uint16_t  mock_resolve_len;
static void     *mock_resolve_result;
static void     *mock_resolve_extra;
static void     *mock_resolve_parent;
static void     *mock_resolve_flags1;
static void     *mock_resolve_flags2;
static void     *mock_resolve_cont;
static void     *mock_resolve_size;
static void     *mock_resolve_last_start;
static void     *mock_resolve_last_size;
static uint32_t  mock_resolve_max;
static void     *mock_resolve_link_count;
static void     *mock_resolve_status;

static int       mock_local_op_calls;

/* case 0x4A/0x4C/0x52/0x54 argument capture (source-wesf) */
static int       mock_set_acl_calls;
static void     *mock_set_acl_uid;
static void     *mock_set_acl_acl;
static void     *mock_set_acl_status;

static int       mock_set_default_acl_calls;
static void     *mock_set_default_acl_p2;
static void     *mock_set_default_acl_p3;

static int       mock_set_def_prot_calls;
static void     *mock_set_def_prot_p2;
static void     *mock_set_def_prot_p3;
static void     *mock_set_def_prot_p4;

static int       mock_prot_audit_calls;
static void     *mock_prot_audit_c;
static void     *mock_prot_audit_d;
static void     *mock_prot_audit_e;
static uint16_t  mock_prot_audit_f;

/* ============================================================================
 * Mocks
 * ============================================================================ */

int16_t HINT_$GET_HINTS(uid_t *uid, uint32_t *addresses)
{
    int16_t i;

    (void)uid;
    for (i = 0; i < mock_hint_count; i++) {
        addresses[i * 2]     = mock_hint_loc[i];
        addresses[i * 2 + 1] = mock_hint_node[i];
    }
    return mock_hint_count;
}

void HINT_$ADDI(uid_t *uid, uint32_t *addresses)
{
    (void)uid;
    mock_addi_calls++;
    mock_addi_loc  = addresses[0];
    mock_addi_node = addresses[1];
}

void DIR_$UPDATE_HINT(uid_t *uid, uint32_t hint1, uint32_t hint2,
                      uid_t *redirect, uint32_t param5)
{
    (void)uid;
    mock_update_hint_calls++;
    mock_update_hint_loc      = hint1;
    mock_update_hint_node     = hint2;
    mock_update_hint_redirect = redirect;
    mock_update_hint_extra    = param5;
}

int8_t DIR_$IS_RETRYABLE_STATUS(status_$t status)
{
    mock_retryable_calls++;
    mock_retryable_arg = status;
    return mock_retryable_result;
}

void REM_FILE_$RN_DO_OP(void *hint, void *request, int16_t req_size,
                        uint16_t resp_size, void *response,
                        uint16_t *received_len)
{
    uint32_t *h = (uint32_t *)hint;
    uint8_t  *r = (uint8_t *)response;
    status_$t st;

    (void)request; (void)resp_size; (void)received_len;

    mock_rn_hint_loc  = h[0];
    mock_rn_hint_node = h[1];
    mock_rn_req_size  = req_size;

    st = mock_rn_status[mock_rn_calls < 7 ? mock_rn_calls : 7];
    mock_rn_calls++;

    memcpy(r + 0x04, &st, sizeof(st));
    *(uint16_t *)(void *)(r + 0x08) = mock_rn_reply_ver;
    *(uint16_t *)(void *)(r + 0x0a) = mock_rn_accepted_ver;
    *(uint16_t *)(void *)(r + 0x14) = mock_rn_len_word;
    memcpy(r + 0x16, &mock_rn_uid_high, 4);
    memcpy(r + 0x1a, &mock_rn_uid_low, 4);
    memcpy(r + 0x1e, &mock_rn_at_1e, 4);
    memcpy(r + 0x30, &mock_rn_at_30, 4);
}

void dir_$do_op_resolve(uint32_t path_data, uint16_t path_len, void *result,
                        uint32_t *extra_ret, uint32_t *parent_uid_ret,
                        uint8_t *flags1, uint8_t *flags2,
                        uint16_t *cont, uint16_t *size,
                        uint16_t *last_start, uint16_t *last_size,
                        uint32_t max, uint16_t *link_count,
                        status_$t *status_ret)
{
    mock_resolve_calls++;
    mock_resolve_path       = path_data;
    mock_resolve_len        = path_len;
    mock_resolve_result     = result;
    mock_resolve_extra      = extra_ret;
    mock_resolve_parent     = parent_uid_ret;
    mock_resolve_flags1     = flags1;
    mock_resolve_flags2     = flags2;
    mock_resolve_cont       = cont;
    mock_resolve_size       = size;
    mock_resolve_last_start = last_start;
    mock_resolve_last_size  = last_size;
    mock_resolve_max        = max;
    mock_resolve_link_count = link_count;
    mock_resolve_status     = status_ret;
    *status_ret = status_$ok;
}

/* Every other local handler: only the call count matters here. */
#define OK(st)  do { mock_local_op_calls++; *(st) = status_$ok; } while (0)

void dir_$do_op_add_bak(uid_t *uid, uint16_t type, void *name_ptr,
                        uint16_t name_len, void *uid_data, uid_t *result_uid,
                        status_$t *st)
{ (void)uid;(void)type;(void)name_ptr;(void)name_len;(void)uid_data;(void)result_uid; OK(st); }

void dir_$do_op_add_entry(uid_t *uid, uint16_t type, void *name,
                          uint16_t name_len, uint16_t entry_type,
                          uint32_t extra, void *uid_data, uint16_t target_len,
                          uint32_t target_data, void *result, status_$t *st)
{ (void)uid;(void)type;(void)name;(void)name_len;(void)entry_type;(void)extra;
  (void)uid_data;(void)target_len;(void)target_data;(void)result; OK(st); }

void dir_$do_op_add_link(uid_t *uid, void *name, uint16_t name_len,
                         uid_t *file_uid, boolean is_hard_link, status_$t *st)
{ (void)uid;(void)name;(void)name_len;(void)file_uid;(void)is_hard_link; OK(st); }

void dir_$do_op_add_mount(uid_t *uid, uid_t *mount_uid, uint32_t node_id,
                          status_$t *st)
{ (void)uid;(void)mount_uid;(void)node_id; OK(st); }

void dir_$do_op_cname(uid_t *uid, uint16_t ver, void *old_name,
                      uint16_t old_len, void *new_name, uint16_t new_len,
                      status_$t *st)
{ (void)uid;(void)ver;(void)old_name;(void)old_len;(void)new_name;(void)new_len; OK(st); }

void dir_$do_op_create_dir(uid_t *uid, void *name, uint16_t name_len,
                           void *result_uid, status_$t *st)
{ (void)uid;(void)name;(void)name_len;(void)result_uid; OK(st); }

void dir_$do_op_delete(uid_t *uid, void *name, uint16_t name_len,
                       boolean f1, boolean f2, boolean f3,
                       uid_t *entry_uid_ret, uid_t *result_uid,
                       status_$t *st)
{ (void)uid;(void)name;(void)name_len;(void)f1;(void)f2;(void)f3;
  (void)entry_uid_ret;(void)result_uid; OK(st); }

void dir_$do_op_dir_readu(uid_t *uid, int16_t version, char *name,
                          uint16_t name_flags, void *cont, uint32_t max_entries,
                          uint32_t max_size, void *buf_ptr, void *size_ret,
                          void *offset_ret, void *count_ret, status_$t *st)
{ (void)uid;(void)version;(void)name;(void)name_flags;(void)cont;(void)max_entries;
  (void)max_size;(void)buf_ptr;(void)size_ret;(void)offset_ret;(void)count_ret; OK(st); }

void dir_$do_op_drop_dir(uid_t *uid, void *name, uint16_t name_len, status_$t *st)
{ (void)uid;(void)name;(void)name_len; OK(st); }

void dir_$do_op_drop_entry(uid_t *uid, uint16_t rights, void *name,
                           uint16_t name_len, uint16_t entry_type,
                           void *result_uid, status_$t *st)
{ (void)uid;(void)rights;(void)name;(void)name_len;(void)entry_type;(void)result_uid; OK(st); }

void dir_$do_op_drop_mount(uid_t *mount_uid, uint32_t node_id, status_$t *st)
{ (void)mount_uid;(void)node_id; OK(st); }

void dir_$do_op_find_uid(uid_t *uid, uid_t *target_uid, int8_t flag,
                         void *name_ret, void *len_ret, void *uid_ret,
                         status_$t *st)
{ (void)uid;(void)target_uid;(void)flag;(void)name_ret;(void)len_ret;(void)uid_ret; OK(st); }

void dir_$do_op_fix_dir(uid_t *uid, status_$t *st) { (void)uid; OK(st); }

void dir_$do_op_get_def_prot(uid_t *uid, void *acl_type, void *prot_buf,
                             void *acl_ret, status_$t *st)
{ (void)uid;(void)acl_type;(void)prot_buf;(void)acl_ret; OK(st); }

void dir_$do_op_get_default_acl(uid_t *uid, uid_t *type, uid_t *acl_ret,
                                status_$t *st)
{ (void)uid;(void)type;(void)acl_ret; OK(st); }

void dir_$do_op_get_entryu(uid_t *uid, void *name, uint16_t name_len,
                           uint16_t *type_ret, uid_t *uid_ret,
                           uint32_t *extra_ret, status_$t *st)
{ (void)uid;(void)name;(void)name_len;(void)type_ret;(void)uid_ret;(void)extra_ret; OK(st); }

void dir_$do_op_read_linku(uid_t *uid, void *name, uint16_t name_len,
                           uint16_t buf_len, uint32_t extra, void *link_type_ret,
                           uid_t *uid_ret, status_$t *st)
{ (void)uid;(void)name;(void)name_len;(void)buf_len;(void)extra;(void)link_type_ret;
  (void)uid_ret; OK(st); }

void dir_$do_op_set_def_prot(uid_t *dir_uid, void *acl_type, void *prot_data,
                             void *src_acl_uid, status_$t *st)
{
    (void)dir_uid;
    mock_set_def_prot_calls++;
    mock_set_def_prot_p2 = acl_type;
    mock_set_def_prot_p3 = prot_data;
    mock_set_def_prot_p4 = src_acl_uid;
    OK(st);
}

void dir_$do_op_set_default_acl(uid_t *dir_uid, void *acl_type, void *src_acl_uid,
                                status_$t *st)
{
    (void)dir_uid;
    mock_set_default_acl_calls++;
    mock_set_default_acl_p2 = acl_type;
    mock_set_default_acl_p3 = src_acl_uid;
    OK(st);
}

void dir_$do_op_set_prot(uid_t *uid, void *prot_data, void *acl_uid,
                         int16_t prot_type, status_$t *st)
{ (void)uid;(void)prot_data;(void)acl_uid;(void)prot_type; OK(st); }

void dir_$do_op_validate_root_entry(void *name, uint16_t name_len, status_$t *st)
{ (void)name;(void)name_len; OK(st); }

/*
 * 0x00E4C78E calls dir_$do_op_set_acl (0x00E52BC2), the server handler - not
 * DIR_$SET_ACL (0x00E52C86), which is the client-side request builder.
 */
void dir_$do_op_set_acl(uid_t *uid, uid_t *acl_uid, status_$t *st)
{
    mock_set_acl_calls++;
    mock_set_acl_uid    = uid;
    mock_set_acl_acl    = acl_uid;
    mock_set_acl_status = st;
    OK(st);
}

char dir_$find_entry(void *handle, void *name, int16_t name_len,
                     int16_t flags, void **entry_ret, void *extra,
                     int16_t *depth_ret)
{ (void)handle;(void)name;(void)name_len;(void)flags;(void)entry_ret;(void)extra;
  (void)depth_ret; return 0; }

void AUDIT_$LOG_DIR_OP(uint16_t a, status_$t b, uid_t *c, uid_t *d,
                       uint16_t e, void *f)
{ (void)a;(void)b;(void)c;(void)d;(void)e;(void)f; }
void AUDIT_$LOG_LINK_OP(uint16_t a, status_$t b, uid_t *c, uint16_t d, void *e,
                        uint16_t f, void *g)
{ (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g; }
void AUDIT_$LOG_CNAME_OP(uint16_t a, status_$t b, uid_t *c, uint16_t d,
                         uint16_t e, void *f, void *g)
{ (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g; }
void audit_$log_mount_op(uint16_t a, status_$t b, uid_t *c, uid_t *d, uint32_t e)
{ (void)a;(void)b;(void)c;(void)d;(void)e; }
void audit_$log_prot_op(status_$t a, uid_t *b, void *c, uid_t *d, uid_t *e,
                        uint16_t f)
{
    (void)a;(void)b;
    mock_prot_audit_calls++;
    mock_prot_audit_c = c;
    mock_prot_audit_d = d;
    mock_prot_audit_e = e;
    mock_prot_audit_f = f;
}
void audit_$log_resolve_op(uint32_t a, uint16_t b, void *c, status_$t d)
{ (void)a;(void)b;(void)c;(void)d; }

void CRASH_SYSTEM(const status_$t *s) { (void)s; }

/* ============================================================================
 * The code under test
 * ============================================================================ */

#include "../do_op.c"

/* ============================================================================
 * Fixtures
 * ============================================================================ */

#define REMOTE_NODE_A   0x00099001u
#define REMOTE_NODE_B   0x00099002u

static uint8_t  req_buf[0x140];
static uint8_t  resp_buf[0x80];
static uint16_t reply_len;

static const uid_t TEST_DIR_UID = { 0x11223344u, 0x0AA55066u };

static void reset(uint8_t op_code)
{
    memset(req_buf, 0, sizeof(req_buf));
    memset(resp_buf, 0, sizeof(resp_buf));
    memset(DIR_$OP_TAB, 0, sizeof(DIR_$OP_TAB));
    memset(PROC1_$DATA.type, 0, sizeof(PROC1_$DATA.type));
    reply_len = 0;

    req_buf[3] = op_code;
    memcpy(req_buf + 4, &TEST_DIR_UID.high, 4);
    memcpy(req_buf + 8, &TEST_DIR_UID.low, 4);

    DIR_$OP_VERSION(op_code >> 1)    = 5;
    DIR_$OP_REPLY_SIZE(op_code >> 1) = 0x20;

    mock_hint_count = 1;
    mock_hint_loc[0]  = 0xAAAA0001u;
    mock_hint_node[0] = REMOTE_NODE_A;
    mock_hint_loc[1]  = 0xAAAA0002u;
    mock_hint_node[1] = REMOTE_NODE_B;

    mock_rn_calls = 0;
    memset(mock_rn_status, 0, sizeof(mock_rn_status));
    mock_rn_reply_ver    = 0;
    mock_rn_accepted_ver = 5;
    mock_rn_uid_high     = 0;
    mock_rn_uid_low      = 0;
    mock_rn_len_word     = 0;
    mock_rn_at_1e        = 0;
    mock_rn_at_30        = 0;

    mock_addi_calls = 0;
    mock_addi_loc = 0;
    mock_addi_node = 0;

    mock_update_hint_calls = 0;
    mock_update_hint_loc = 0;
    mock_update_hint_node = 0;
    mock_update_hint_extra = 0xDEADBEEFu;
    mock_update_hint_redirect = NULL;

    mock_retryable_calls = 0;
    mock_retryable_arg = 0;
    mock_retryable_result = 0;

    mock_resolve_calls = 0;
    mock_local_op_calls = 0;

    mock_set_acl_calls = 0;
    mock_set_acl_uid = NULL;
    mock_set_acl_acl = NULL;
    mock_set_acl_status = NULL;
    mock_set_default_acl_calls = 0;
    mock_set_default_acl_p2 = NULL;
    mock_set_default_acl_p3 = NULL;
    mock_set_def_prot_calls = 0;
    mock_set_def_prot_p2 = NULL;
    mock_set_def_prot_p3 = NULL;
    mock_set_def_prot_p4 = NULL;
    mock_prot_audit_calls = 0;
    mock_prot_audit_c = NULL;
    mock_prot_audit_d = NULL;
    mock_prot_audit_e = NULL;
    mock_prot_audit_f = 0;
}

static void run(void)
{
    DIR_$DO_OP(req_buf, 0x10, 0x40, resp_buf, &reply_len);
}

static status_$t resp_status(void)
{
    status_$t s;
    memcpy(&s, resp_buf + 4, sizeof(s));
    return s;
}

/* ============================================================================
 * The reply-version test (0x00E4C174)
 * ============================================================================ */

TEST(reply_with_a_positive_version_word_is_rejected)
{
    reset(TEST_OP_DELETE);
    mock_rn_reply_ver = 1;                  /* tst.w (0x8,A3); bgt */
    run();
    ASSERT_EQ(file_$bad_reply_received_from_remote_node, resp_status());
}

TEST(reply_whose_accepted_version_is_too_high_is_rejected)
{
    reset(TEST_OP_DELETE);
    mock_rn_accepted_ver = 6;               /* > DIR_$OP_VERSION = 5 */
    run();
    ASSERT_EQ(file_$bad_reply_received_from_remote_node, resp_status());

    /* Equal is accepted (`ble` keeps the reply). */
    reset(TEST_OP_DELETE);
    mock_rn_accepted_ver = 5;
    run();
    ASSERT_EQ(status_$ok, resp_status());

    /* And so is lower - the old code rejected this one. */
    reset(TEST_OP_DELETE);
    mock_rn_accepted_ver = 1;
    run();
    ASSERT_EQ(status_$ok, resp_status());
}

/* ============================================================================
 * HINT_$ADDI indexing (0x00E4C19A / 0x00E4C1A6)
 * ============================================================================ */

TEST(first_hint_success_does_not_re_add_the_hint)
{
    reset(TEST_OP_DELETE);
    run();
    ASSERT_EQ(0, mock_addi_calls);
}

TEST(second_hint_success_re_adds_that_hints_pair)
{
    reset(TEST_OP_DELETE);
    mock_hint_count = 2;
    mock_rn_status[0] = 0x000F0004;     /* retryable -> next hint */
    mock_retryable_result = (int8_t)0xFF;
    run();
    ASSERT_EQ(2, mock_rn_calls);
    ASSERT_EQ(REMOTE_NODE_B, mock_rn_hint_node);
    ASSERT_EQ(1, mock_addi_calls);
    /* The pair just used, not the one after it. */
    ASSERT_EQ(0xAAAA0002u, mock_addi_loc);
    ASSERT_EQ(REMOTE_NODE_B, mock_addi_node);
}

/* ============================================================================
 * DIR_$IS_RETRYABLE_STATUS gets the full longword (0x00E4C150)
 * ============================================================================ */

TEST(is_retryable_status_receives_the_whole_longword)
{
    reset(TEST_OP_DELETE);
    mock_rn_status[0] = 0x00110001;     /* low word alone would be 0x0001 */
    mock_retryable_result = 0;          /* not retryable -> return */
    run();
    ASSERT_EQ(1, mock_retryable_calls);
    ASSERT_EQ(0x00110001u, (uint32_t)mock_retryable_arg);
}

TEST(a_non_retryable_error_stops_after_the_first_hint)
{
    reset(TEST_OP_DELETE);
    mock_hint_count = 2;
    mock_rn_status[0] = 0x000F0011;
    mock_retryable_result = 0;
    run();
    ASSERT_EQ(1, mock_rn_calls);
    ASSERT_EQ(0x000F0011, resp_status());
}

/* ============================================================================
 * DIR_$UPDATE_HINT (0x00E4C1C8-0x00E4C23C)
 * ============================================================================ */

TEST(resolve_redirect_forwards_the_longword_at_reply_0x30)
{
    reset(TEST_OP_RESOLVE);
    mock_rn_uid_high = 0x55000000u;     /* reply+0x16 top byte non-zero */
    mock_rn_uid_low  = 0x000FFFFFu;     /* different node from the request */
    mock_rn_at_30    = 0x0BADF00Du;
    run();
    ASSERT_EQ(1, mock_update_hint_calls);
    ASSERT_EQ(0x0BADF00Du, mock_update_hint_extra);
    /* Both hint words go by value, from the pair the request used. */
    ASSERT_EQ(0xAAAA0001u, mock_update_hint_loc);
    ASSERT_EQ(REMOTE_NODE_A, mock_update_hint_node);
    ASSERT_EQ(1, (void *)(resp_buf + 0x16) == mock_update_hint_redirect);
}

TEST(get_entryu_redirect_forwards_the_longword_at_reply_0x1e)
{
    reset(TEST_OP_GET_ENTRYU);
    mock_rn_uid_high = 0x55000000u;
    mock_rn_uid_low  = 0x000FFFFFu;
    mock_rn_len_word = 1;               /* cmpi.w #0x1,(0x14,A3) */
    mock_rn_at_1e    = 0xFEEDFACEu;
    mock_rn_at_30    = 0x0BADF00Du;     /* must NOT be used for op 0x44 */
    run();
    ASSERT_EQ(1, mock_update_hint_calls);
    ASSERT_EQ(0xFEEDFACEu, mock_update_hint_extra);
}

TEST(get_entryu_redirect_needs_a_length_word_of_one)
{
    reset(TEST_OP_GET_ENTRYU);
    mock_rn_uid_high = 0x55000000u;
    mock_rn_uid_low  = 0x000FFFFFu;
    mock_rn_len_word = 2;
    run();
    ASSERT_EQ(0, mock_update_hint_calls);
}

TEST(an_operation_that_is_neither_0x58_nor_0x44_never_redirects)
{
    reset(TEST_OP_DELETE);
    mock_rn_uid_high = 0x55000000u;
    mock_rn_uid_low  = 0x000FFFFFu;
    mock_rn_len_word = 1;
    run();
    ASSERT_EQ(0, mock_update_hint_calls);
}

/* 0x00E4C1C4: a zero top byte in the reply's UID stops the whole thing. */
TEST(a_nil_reply_uid_skips_the_redirect_entirely)
{
    reset(TEST_OP_RESOLVE);
    mock_rn_uid_high = 0x00FFFFFFu;     /* top byte zero */
    mock_rn_uid_low  = 0x000FFFFFu;
    mock_rn_at_30    = 0x0BADF00Du;
    run();
    ASSERT_EQ(0, mock_update_hint_calls);
}

/* 0x00E4C1DA: a RESOLVE whose reply names the SAME node falls into the
 * 0x44-only branch and returns. */
TEST(resolve_to_the_same_node_does_not_redirect)
{
    reset(TEST_OP_RESOLVE);
    mock_rn_uid_high = 0x55000000u;
    mock_rn_uid_low  = TEST_DIR_UID.low;
    mock_rn_at_30    = 0x0BADF00Du;
    run();
    ASSERT_EQ(0, mock_update_hint_calls);
}

/* ============================================================================
 * The 0x000E0016 retry loop (0x00E4C134) and the stale fallback (0x00E4C9BE)
 * ============================================================================ */

TEST(locked_directory_retries_the_same_hint_twenty_times)
{
    int i;

    reset(TEST_OP_DELETE);
    mock_hint_count = 2;
    for (i = 0; i < 8; i++) {
        mock_rn_status[i] = status_$naming_directory_locked;   /* 0x000E0016 */
    }
    run();
    ASSERT_EQ(20, mock_rn_calls);               /* cmpi.w #0x14 */
    ASSERT_EQ(REMOTE_NODE_A, mock_rn_hint_node);/* never advanced */
    ASSERT_EQ(status_$naming_directory_locked, resp_status());
}

TEST(local_node_dispatches_and_fills_the_reply_header)
{
    reset(TEST_OP_DELETE);
    mock_hint_node[0] = NODE_$ME;
    run();
    ASSERT_EQ(0, mock_rn_calls);
    ASSERT_EQ(1, mock_local_op_calls);
    ASSERT_EQ(0x20 + 0x14, reply_len);          /* 0x00E4C24E */
    ASSERT_EQ(5, *(uint16_t *)(void *)(resp_buf + 0x0a));
    ASSERT_EQ(0, *(uint16_t *)(void *)(resp_buf + 0x08));
}

/* 0x00E4C9BE: a server process (PROC1_$TYPE == 9) never tries another hint. */
TEST(a_server_process_does_not_fall_back_after_a_stale_object)
{
    reset(TEST_OP_DELETE);
    PROC1_$DATA.type[PROC1_$CURRENT] = 9;    /* hint_count becomes 1, node = ME */
    run();
    ASSERT_EQ(0, mock_rn_calls);
    ASSERT_EQ(1, mock_local_op_calls);
}

/* ============================================================================
 * case 0x58: dir_$do_op_resolve's fourteen arguments (0x00E4C894)
 * ============================================================================ */

TEST(resolve_handler_arguments_follow_the_push_order)
{
    uint32_t path;
    int i;

    reset(TEST_OP_RESOLVE);
    mock_hint_node[0] = NODE_$ME;

    path = 0x12345678u;
    memcpy(req_buf + 0x8e, &path, 4);
    *(uint16_t *)(void *)(req_buf + 0x92) = 0x0042;
    for (i = 0; i < 24; i++) {
        req_buf[0x94 + i] = (uint8_t)(0xC0 + i);
    }
    *(uint32_t *)(void *)(req_buf + 0xac) = 0x99887766u;

    run();

    ASSERT_EQ(1, mock_resolve_calls);
    ASSERT_EQ(0x12345678u, mock_resolve_path);
    ASSERT_EQ(0x0042, mock_resolve_len);
    ASSERT_EQ(1, mock_resolve_result     == (void *)(resp_buf + 0x16));
    ASSERT_EQ(1, mock_resolve_extra      == (void *)(resp_buf + 0x30));
    ASSERT_EQ(1, mock_resolve_parent     == (void *)(resp_buf + 0x1e));
    ASSERT_EQ(1, mock_resolve_flags1     == (void *)(resp_buf + 0x14));
    ASSERT_EQ(1, mock_resolve_flags2     == (void *)(resp_buf + 0x15));
    ASSERT_EQ(1, mock_resolve_cont       == (void *)(resp_buf + 0x26));
    ASSERT_EQ(1, mock_resolve_size       == (void *)(resp_buf + 0x28));
    ASSERT_EQ(1, mock_resolve_last_start == (void *)(resp_buf + 0x2a));
    ASSERT_EQ(1, mock_resolve_last_size  == (void *)(resp_buf + 0x2c));
    ASSERT_EQ(0x99887766u, mock_resolve_max);   /* req+0xAC, not reply+0x0A */
    ASSERT_EQ(1, mock_resolve_link_count == (void *)(resp_buf + 0x2e));
    ASSERT_EQ(1, mock_resolve_status     == (void *)(resp_buf + 0x04));

    /* 0x00E4C884: 0x18 bytes of the request are copied to reply+0x16 first. */
    ASSERT_EQ(0xC0, resp_buf[0x16]);
    ASSERT_EQ(0xC0 + 23, resp_buf[0x16 + 23]);
}

/* ============================================================================
 * cases 0x4A / 0x4C / 0x52 / 0x54: the ACL and protection handlers
 * (source-wesf).  Every argument here is a pea of req+off or reply+off, so
 * the assertions compare pointers against the buffers the test owns.
 * ============================================================================ */

/* 0x00E4C782-0x00E4C78E: pea (0x4,A3) / pea (0x8e,A2) / pea (-0x10,A6) then
   bsr.w 0x00E52BC2 = dir_$do_op_set_acl, the SERVER handler. */
TEST(set_acl_calls_the_server_handler_with_req_0x8e)
{
    reset(0x4A);
    mock_hint_node[0] = NODE_$ME;
    run();
    ASSERT_EQ(1, mock_set_acl_calls);
    ASSERT_EQ(1, mock_set_acl_acl    == (void *)(req_buf + 0x8e));
    ASSERT_EQ(1, mock_set_acl_status == (void *)(resp_buf + 0x04));
}

/* 0x00E4C794-0x00E4C7A4: pea (0x4,A3) / pea (0x96,A2) / pea (0x8e,A2) /
   pea (-0x10,A6), so param_2 = req+0x8E and param_3 = req+0x96.

   Roles come from dir_$set_default_acl_internal (0x00E52D70): param_2 lands
   in D5 and is compared as two longwords against ACL_$DIR_ACL / ACL_$FILE_ACL
   at 0x00E52E64 / 0x00E52EB4, so req+0x8E is an 8-byte ACL TYPE uid; param_3
   lands in A2 and is the source ACL uid (funky test at 0x00E52DA0).  The two
   uids are therefore adjacent, 0x96 - 0x8E = 8. */
TEST(set_default_acl_takes_req_0x8e_then_req_0x96)
{
    reset(0x4C);
    mock_hint_node[0] = NODE_$ME;
    run();
    ASSERT_EQ(1, mock_set_default_acl_calls);
    ASSERT_EQ(1, mock_set_default_acl_p2 == (void *)(req_buf + 0x8e));
    ASSERT_EQ(1, mock_set_default_acl_p3 == (void *)(req_buf + 0x96));
    /* acl_type is a whole 8-byte uid, so src_acl_uid starts right after it */
    ASSERT_EQ(8, (int)((char *)mock_set_default_acl_p3 -
                       (char *)mock_set_default_acl_p2));
}

/* 0x00E4C81C-0x00E4C830: pea (0x4,A3) / pea (0xc2,A2) / pea (0x96,A2) /
   pea (0x8e,A2) / pea (-0x10,A6).

   Roles come from dir_$write_def_prot (0x00E51E18): param_2 lands in D5 and
   is compared against ACL_$DIR_ACL / ACL_$FILE_ACL at 0x00E51F2A / 0x00E51F7C
   (8-byte ACL TYPE uid); param_3 is copied as 11 longwords = 44 bytes by the
   loop at 0x00E51E7C-0x00E51E88 (default-protection data); param_4 lands in
   A2 and is the source ACL uid (funky test at 0x00E51E48, 8 bytes copied at
   0x00E51E8C).  Hence 0x96 - 0x8E = 8 and 0xC2 - 0x96 = 44. */
TEST(set_def_prot_takes_req_0x8e_0x96_0xc2_in_that_order)
{
    reset(0x54);
    mock_hint_node[0] = NODE_$ME;
    run();
    ASSERT_EQ(1, mock_set_def_prot_calls);
    ASSERT_EQ(1, mock_set_def_prot_p2 == (void *)(req_buf + 0x8e));
    ASSERT_EQ(1, mock_set_def_prot_p3 == (void *)(req_buf + 0x96));
    ASSERT_EQ(1, mock_set_def_prot_p4 == (void *)(req_buf + 0xc2));
    /* acl_type is an 8-byte uid; prot_data is the 44 bytes after it */
    ASSERT_EQ(8, (int)((char *)mock_set_def_prot_p3 -
                       (char *)mock_set_def_prot_p2));
    ASSERT_EQ(44, (int)((char *)mock_set_def_prot_p4 -
                        (char *)mock_set_def_prot_p3));
}

/* 0x00E4C842-0x00E4C85C, the case 0x54 audit tail:
   move.w #0x4 / pea (0xc2,A2) / pea (0x8e,A2) / pea (0x96,A2). */
TEST(set_def_prot_audit_uses_req_0x96_0x8e_0xc2_and_flags_4)
{
    reset(0x54);
    mock_hint_node[0] = NODE_$ME;
    AUDIT_$ENABLED = -1;                /* bmi at 0x00E4C83E */
    run();
    AUDIT_$ENABLED = 0;
    ASSERT_EQ(1, mock_prot_audit_calls);
    ASSERT_EQ(1, mock_prot_audit_c == (void *)(req_buf + 0x96));
    ASSERT_EQ(1, mock_prot_audit_d == (void *)(req_buf + 0x8e));
    ASSERT_EQ(1, mock_prot_audit_e == (void *)(req_buf + 0xc2));
    ASSERT_EQ(4, mock_prot_audit_f);
}

/* 0x00E4C806-0x00E4C81A, the case 0x52 audit tail: the third argument is the
   canned uid ACL_$DIRIN_ACL (move.l #0xe1745c), the fifth is req+0xBA and the
   sixth is the WORD at req+0xC2 - not the constant 4. */
TEST(set_prot_audit_passes_dirin_acl_and_the_word_at_req_0xc2)
{
    reset(0x52);
    mock_hint_node[0] = NODE_$ME;
    *(uint16_t *)(void *)(req_buf + 0xc2) = 0x0037;
    AUDIT_$ENABLED = -1;                /* bmi at 0x00E4C802 */
    run();
    AUDIT_$ENABLED = 0;
    ASSERT_EQ(1, mock_prot_audit_calls);
    ASSERT_EQ(1, mock_prot_audit_c == (void *)(req_buf + 0x8e));
    ASSERT_EQ(1, mock_prot_audit_d == (void *)&ACL_$DIRIN_ACL);
    ASSERT_EQ(1, mock_prot_audit_e == (void *)(req_buf + 0xba));
    ASSERT_EQ(0x0037, mock_prot_audit_f);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("DIR_$DO_OP (0x00E4C02C) tests\n");

    RUN_TEST(reply_with_a_positive_version_word_is_rejected);
    RUN_TEST(reply_whose_accepted_version_is_too_high_is_rejected);

    RUN_TEST(first_hint_success_does_not_re_add_the_hint);
    RUN_TEST(second_hint_success_re_adds_that_hints_pair);

    RUN_TEST(is_retryable_status_receives_the_whole_longword);
    RUN_TEST(a_non_retryable_error_stops_after_the_first_hint);

    RUN_TEST(resolve_redirect_forwards_the_longword_at_reply_0x30);
    RUN_TEST(get_entryu_redirect_forwards_the_longword_at_reply_0x1e);
    RUN_TEST(get_entryu_redirect_needs_a_length_word_of_one);
    RUN_TEST(an_operation_that_is_neither_0x58_nor_0x44_never_redirects);
    RUN_TEST(a_nil_reply_uid_skips_the_redirect_entirely);
    RUN_TEST(resolve_to_the_same_node_does_not_redirect);

    RUN_TEST(locked_directory_retries_the_same_hint_twenty_times);
    RUN_TEST(local_node_dispatches_and_fills_the_reply_header);
    RUN_TEST(a_server_process_does_not_fall_back_after_a_stale_object);

    RUN_TEST(resolve_handler_arguments_follow_the_push_order);

    RUN_TEST(set_acl_calls_the_server_handler_with_req_0x8e);
    RUN_TEST(set_default_acl_takes_req_0x8e_then_req_0x96);
    RUN_TEST(set_def_prot_takes_req_0x8e_0x96_0xc2_in_that_order);
    RUN_TEST(set_def_prot_audit_uses_req_0x96_0x8e_0xc2_and_flags_4);
    RUN_TEST(set_prot_audit_passes_dirin_acl_and_the_word_at_req_0xc2);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
