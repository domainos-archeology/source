/*
 * acl/test/test_server.c - unit tests for ACL_$SERVER (0x00E49594)
 */

#include <stdio.h>
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

#include "acl/acl_internal.h"

MODULE_DATA_DEFINE(acl_$unwired_data_t, ACL_$UNWIRED_DATA, 0x00E7CF54);
uid_t UID_$NIL = { 0, 0 };

/* ---- mocks ----------------------------------------------------------- */

static char trace[256];
static void tr(const char *s) { strcat(trace, s); strcat(trace, " "); }

static status_$t st_funky, st_image, st_create_int, st_get_sids, st_get_proj,
                 st_set_sids, st_set_proj, st_check;
static void *image_buf_arg, *create_int_image;
static const void *create_int_data;
static int8_t image_flag_arg, create_int_flag;
static int16_t create_len_arg;
static uint32_t eval_mask;
static int16_t eval_opts;
static int16_t proj_max_seen;

void ML_$LOCK(int16_t id) { (void)id; tr("lock"); }
void ML_$UNLOCK(int16_t id) { (void)id; tr("unlock"); }
void ACL_$ENTER_SUPER(void) { tr("enter"); }
void ACL_$EXIT_SUPER(void) { tr("exit"); }

void ACL_$CONVERT_FUNKY_ACL(void *acl_uid, void *acl_data_out,
                            void *prot_info_out, void *target_uid_out,
                            status_$t *status_ret)
{
    (void)acl_uid;
    tr("funky");
    ((acl_$prot_data_t *)acl_data_out)->owner.high = 0xF0;
    ((uid_t *)prot_info_out)->high = 0;        /* a NIL ACL */
    ((uid_t *)prot_info_out)->low = 0;
    ((uid_t *)target_uid_out)->high = 0x5B;
    *status_ret = st_funky;
}

void acl_$image_internal(uid_t *source_uid, int16_t buffer_len, int8_t flag,
                         void *output_buf, int16_t *len_out,
                         acl_$prot_data_t *data_out, int8_t *flag_out,
                         status_$t *status)
{
    (void)source_uid; (void)buffer_len; (void)flag_out;
    tr("image");
    image_flag_arg = flag;
    image_buf_arg = output_buf;
    *(int16_t *)len_out = 0x74;
    ((acl_$prot_data_t *)data_out)->owner.high = 0x1234;
    *status = st_image;
}

void acl_$prim_create_internal(acl_$prot_data_t *prot, const void *src_image,
                               int16_t src_len, uid_t *acl_type, int8_t flag,
                               void *image, int16_t *image_len_ret,
                               status_$t *status_ret)
{
    (void)acl_type;
    tr("pci");
    create_int_data = src_image;
    create_int_flag = flag;
    create_int_image = image;
    ASSERT_EQ(0x74, src_len);
    ASSERT_EQ(0xF0, prot->owner.high);
    *image_len_ret = 0x60;
    *status_ret = st_create_int;
}

void ACL_$PRIM_CREATE(void *acl_data, int16_t *data_len, uid_t *dir_uid,
                      void *type, uid_t *file_uid_ret, status_$t *status_ret)
{
    (void)acl_data; (void)dir_uid; (void)type;
    tr("create");
    create_len_arg = *data_len;
    file_uid_ret->low = 0x77;
    *status_ret = status_$ok;
}

uint32_t acl_$eval_rights(acl_sid_block_t *sids, uid_t *proj_uids, uid_t *uid,
                          boolean ignore_super, uint32_t required_mask,
                          int16_t option_flags, boolean in_super,
                          boolean in_subsys, status_$t *status_ret)
{
    (void)sids; (void)proj_uids; (void)uid; (void)ignore_super;
    (void)in_super; (void)in_subsys;
    tr("eval");
    eval_mask = required_mask;
    eval_opts = option_flags;
    *status_ret = status_$ok;
    return 0x0F;
}

void acl_$setids(uid_t *uid, int8_t set, uid_t *sids, uint32_t *owner_ext,
                 int8_t *changed, status_$t *status_ret)
{
    (void)uid; (void)owner_ext;
    tr("setids");
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)set);
    sids[0].high = 0xAB;
    *changed = (int8_t)0xFF;
    *status_ret = status_$ok;
}

void ACL_$GET_RE_ALL_SIDS(void *a, void *b, void *c, void *d, status_$t *s)
{ (void)a; (void)b; (void)c; (void)d; tr("getsids"); *s = st_get_sids; }
void ACL_$GET_PROJ_LIST(uid_t *p, int16_t *max, int16_t *count, status_$t *s)
{ (void)p; tr("getproj"); proj_max_seen = *max; *count = 2; *s = st_get_proj; }
void ACL_$SET_RE_ALL_SIDS(void *a, void *b, void *c, void *d, status_$t *s)
{ (void)a; (void)b; (void)c; (void)d; tr("setsids"); *s = st_set_sids; }
void ACL_$SET_PROJ_LIST(uid_t *p, int16_t *count, status_$t *s)
{ (void)p; (void)count; tr("setproj"); *s = st_set_proj; }
boolean ACL_$SET_ACL_CHECK(uid_t *obj_uid, acl_$prot_data_t *new_prot,
                           uid_t *acl_uid, int16_t *op_type,
                           boolean *setid_ret, status_$t *status_ret)
{
    (void)obj_uid; (void)new_prot; (void)acl_uid; (void)op_type;
    tr("check");
    *setid_ret = 0;
    *status_ret = st_check;
    return (boolean)0xFF;
}

#include "../server.c"

/* ---- helpers --------------------------------------------------------- */

static uint32_t reqw[0x298 / 4];
static acl_$srv_resp_t resp;
static uint16_t rlen;
static uint8_t page[0x400];
static uint8_t acl_data_page[0x100];

#define HDR  ((acl_$srv_hdr_t *)(void *)reqw)

static void reset_state(void)
{
    memset(reqw, 0, sizeof(reqw));
    memset(&resp, 0, sizeof(resp));
    memset(&ACL_$UNWIRED_DATA, 0, sizeof(ACL_$UNWIRED_DATA));
    trace[0] = 0;
    st_funky = st_image = st_create_int = st_get_sids = st_get_proj =
        st_set_sids = st_set_proj = st_check = status_$ok;
    ARCH_HOST_VA_BASE = (uintptr_t)page - 0x10000u;
    rlen = 0;
}

static void serve(uint8_t op)
{
    HDR->opcode = op;
    ACL_$SERVER(reqw, &resp, &rlen);
}

/* ---- tests ----------------------------------------------------------- */

static void test_header_and_bad_opcode(void)
{
    serve(0x65);
    ASSERT_EQ(0x3C, rlen);
    ASSERT_EQ(1, resp.one);
    ASSERT_EQ(0x80, resp.flags);
    ASSERT_EQ(3, resp.opcode);
    ASSERT_EQ(file_$bad_reply_received_from_remote_node, resp.status);
    serve(0x6D);
    ASSERT_EQ(3, resp.opcode);
    serve(0x63);
    ASSERT_EQ(3, resp.opcode);
}

static void test_image_plain(void)
{
    acl_$srv_image_req_t *r = (acl_$srv_image_req_t *)(void *)reqw;
    r->buf = 0x10000;
    r->flag = 0;
    serve(0x64);
    ASSERT_EQ(0x65, resp.opcode);
    ASSERT_EQ(0x38, rlen);
    ASSERT_EQ(0, strcmp(trace, "lock image unlock "));
    ASSERT_EQ((uintptr_t)page, (uintptr_t)image_buf_arg);
    ASSERT_EQ(0x74, resp.a.image_len);
    ASSERT_EQ(0x1234, ((acl_$prot_data_t *)(void *)resp.data)->owner.high);
    ASSERT_EQ(status_$ok, resp.status);
}

static void test_image_rebuild_funky(void)
{
    acl_$srv_image_req_t *r = (acl_$srv_image_req_t *)(void *)reqw;
    HDR->uid.low = 0x06000000;                  /* type bits 0x60 */
    r->mode = 4;
    r->buf = 0x10000;
    serve(0x64);
    ASSERT_EQ(0, strcmp(trace, "funky lock image pci unlock "));
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)r->flag);
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)image_flag_arg);
    ASSERT_EQ((uintptr_t)&ACL_$UNWIRED_DATA.image_buf, (uintptr_t)image_buf_arg);
    ASSERT_EQ((uintptr_t)&ACL_$UNWIRED_DATA.image_buf, (uintptr_t)create_int_data);
    ASSERT_EQ((uintptr_t)page, (uintptr_t)create_int_image);
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)create_int_flag);
    ASSERT_EQ(0x5B, ACL_$UNWIRED_DATA.image_buf.subsys_uid.high);
    ASSERT_EQ(0x5B, ACL_$UNWIRED_DATA.image_buf.type_uid.high);
    ASSERT_EQ(0x60, resp.a.image_len);
    /* the reply data is the image_internal copy, before the funky swap */
    ASSERT_EQ(0x1234, ((acl_$prot_data_t *)(void *)resp.data)->owner.high);
}

static void test_image_funky_failure_skips_lock(void)
{
    HDR->uid.low = 0x0E000000;                  /* type bits 0xE0 */
    st_funky = 0x00230003;
    serve(0x64);
    ASSERT_EQ(0, strcmp(trace, "funky "));
    ASSERT_EQ(0x00230003, resp.status);
    ASSERT_EQ(0x38, rlen);
}

static void test_create(void)
{
    acl_$srv_create_req_t *r = (acl_$srv_create_req_t *)(void *)reqw;
    ARCH_HOST_VA_BASE = (uintptr_t)acl_data_page - 0x20000u;
    r->acl_data = 0x20000;
    *(int16_t *)(void *)(acl_data_page + 0xE) = 3;
    r->acl_uid.high = 0x44;
    serve(0x68);
    ASSERT_EQ(3 * 0x20 + 0x34, create_len_arg);
    ASSERT_EQ(0x44, ((uid_t *)(void *)resp.data)->high);
    ASSERT_EQ(0x77, ((uid_t *)(void *)resp.data)->low);
    ASSERT_EQ(0x14, rlen);
    ASSERT_EQ(0x69, resp.opcode);
}

static void test_check_rights(void)
{
    acl_$srv_rights_req_t *r = (acl_$srv_rights_req_t *)(void *)reqw;
    r->required_mask = 0x5;
    r->option_flags = 0x10;
    serve(0x6C);
    ASSERT_EQ(0x0F, *(uint32_t *)(void *)resp.data);
    ASSERT_EQ(5, eval_mask);
    ASSERT_EQ(0x10, eval_opts);
    ASSERT_EQ(0x10, rlen);
}

static void test_setids(void)
{
    serve(0x6A);
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)resp.a.changed);
    ASSERT_EQ(0xAB, ((uid_t *)(void *)resp.data)->high);  /* copied back */
    ASSERT_EQ(0x3C, rlen);
}

static void test_set_acl_success(void)
{
    serve(0x66);
    ASSERT_EQ(0, strcmp(trace, "enter getsids getproj setsids setproj exit check "
                               "enter setsids setproj exit "));
    ASSERT_EQ(8, proj_max_seen);
    ASSERT_EQ(8, rlen);
}

static void test_set_acl_setproj_failure(void)
{
    st_set_proj = 0x00230011;
    serve(0x66);
    /* SIDs restored, project list not */
    ASSERT_EQ(0, strcmp(trace, "enter getsids getproj setsids setproj setsids exit "));
    ASSERT_EQ(0x00230011, resp.status);
}

static void test_set_acl_getsids_failure(void)
{
    st_get_sids = 0x00230001;
    serve(0x66);
    ASSERT_EQ(0, strcmp(trace, "enter getsids exit "));
    ASSERT_EQ(8, rlen);
}

static void test_set_acl_check_failure_reported(void)
{
    st_check = 0x00230001;
    serve(0x66);
    ASSERT_EQ(0x00230001, resp.status);
}

int main(void)
{
    printf("ACL_$SERVER tests:\n");
    RUN_TEST(header_and_bad_opcode);
    RUN_TEST(image_plain);
    RUN_TEST(image_rebuild_funky);
    RUN_TEST(image_funky_failure_skips_lock);
    RUN_TEST(create);
    RUN_TEST(check_rights);
    RUN_TEST(setids);
    RUN_TEST(set_acl_success);
    RUN_TEST(set_acl_setproj_failure);
    RUN_TEST(set_acl_getsids_failure);
    RUN_TEST(set_acl_check_failure_reported);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
