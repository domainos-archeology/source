/*
 * mst/test/test_lookup_object.c - unit tests for mst_$lookup_object
 * (0x00E43CBE)
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

#include "mst/mst_internal.h"
#include "ast/ast.h"
#include "file/file.h"
#include "network/network.h"
#include "acl/acl.h"

uint16_t mst_$prot_rights[MST_PROT_RIGHTS_COUNT] = { 0, 1, 4, 5, 2, 3, 6, 7 };

static char log_buf[64];
static void note(const char *s) { strcat(log_buf, s); }
static status_$t attr_status, net_status, prot_status;
static int8_t loc_flags;
static uint8_t loc_byte1c, attr_lo, attr_sub;
static uint32_t loc_node, loc_info, seen_node;
static uint16_t seen_rights;
static uint32_t seen_slot;
static uid_t seen_uid;

void AST_$GET_COMMON_ATTRIBUTES(file_$obj_loc_t *loc_rec, uint16_t flags,
                                ast_$common_attr_t *attrs, status_$t *status)
{
    note("A");
    if (flags != 2 || (loc_rec->flags & 0x40)) note("!");
    seen_uid = loc_rec->uid;
    loc_rec->flags = loc_flags;
    loc_rec->rights_bits = (int8_t)loc_byte1c;
    loc_rec->node = loc_node;
    loc_rec->loc_info = loc_info;
    memset(attrs, 0, sizeof(*attrs));
    attrs->attr_flags_lo = attr_lo;
    attrs->sub_type = attr_sub;
    attrs->length = 0x8000;
    *status = attr_status;
}
void NETWORK_$INSTALL_NET(uint32_t node, uint32_t *info, status_$t *status)
{
    note("N");
    seen_node = node;
    *info = 0x0AB00000 | 0x55;           /* index bits + junk the caller masks */
    *status = net_status;
}
int16_t FILE_$CHECK_PROT(uid_t *file_uid, uint16_t access_mask, uint32_t slot_num,
                         boolean ignore_super, int16_t option_flags,
                         uint16_t *rights_out, status_$t *status_ret)
{
    note("P");
    if (ignore_super || option_flags) note("!");
    seen_uid = *file_uid;
    seen_rights = access_mask;
    seen_slot = slot_num;
    *rights_out = 0;
    *status_ret = prot_status;
    return 1;
}

#include "../lookup_object.c"

static uid_t uid;
static uint32_t location, len;
static status_$t st;

static void reset_state(void)
{
    log_buf[0] = 0;
    attr_status = net_status = prot_status = 0;
    loc_flags = 0; loc_byte1c = 0x2A; attr_lo = 0; attr_sub = 1;
    loc_node = 0x12345; loc_info = 0x77;
    uid.high = 0x100; uid.low = 0x200;
    location = len = 0xDEAD;
    st = 0x1111;
}

static void test_zero_uid_not_found(void)
{
    uid.high = 0;
    mst_$lookup_object(&uid, 2, 0x10, &location, &len, &st);
    ASSERT_EQ(status_$mst_object_not_found, st);
    ASSERT_EQ(0, strlen(log_buf));
}

static void test_local_object_checked(void)
{
    mst_$lookup_object(&uid, 0xA, 0x10, &location, &len, &st);
    ASSERT_EQ(0, strcmp(log_buf, "AP"));
    ASSERT_EQ(0x2A, location);
    ASSERT_EQ(0x8000, len);
    ASSERT_EQ(4, seen_rights);           /* table[0xA & 7] */
    ASSERT_EQ(0x10, seen_slot);
    ASSERT_EQ(0x100, seen_uid.high);
    ASSERT_EQ(0, st);
}

static void test_remote_object_location(void)
{
    loc_flags = (int8_t)0x80;
    attr_sub = 3;
    mst_$lookup_object(&uid, 1, 0x10, &location, &len, &st);
    ASSERT_EQ(0, strcmp(log_buf, "AN"));
    ASSERT_EQ(0x77, seen_node);
    ASSERT_EQ(0x8AB00000u | 0x12345, location);
    ASSERT_EQ(0, st);
}

static void test_remote_install_error_flagged(void)
{
    loc_flags = (int8_t)0x80;
    net_status = 0x00110003;
    mst_$lookup_object(&uid, 1, 0x10, &location, &len, &st);
    ASSERT_EQ(0x80110003u, (uint32_t)st);
    ASSERT_EQ(0xDEAD, len);
}

static void test_attr_errors(void)
{
    attr_status = status_$file_object_not_found;
    mst_$lookup_object(&uid, 1, 0x10, &location, &len, &st);
    ASSERT_EQ(status_$mst_object_not_found, st);
    attr_status = 0x00020005;
    mst_$lookup_object(&uid, 1, 0x10, &location, &len, &st);
    ASSERT_EQ(0x80020005u, (uint32_t)st);
    ASSERT_EQ(0xDEAD, location);
}

static void test_read_only_volume(void)
{
    attr_lo = 0x02;
    mst_$lookup_object(&uid, 0x0C, 0x10, &location, &len, &st);  /* 0xC & 0x14 = 4 */
    ASSERT_EQ(status_$file_volume_has_been_mounted_read_only, st);
    ASSERT_EQ(0xDEAD, location);
    mst_$lookup_object(&uid, 0x1C, 0x10, &location, &len, &st);  /* & 0x14 = 0x14 */
    ASSERT_EQ(0, st);
}

static void test_rights_status_mapping(void)
{
    prot_status = status_$no_right_to_perform_operation;
    mst_$lookup_object(&uid, 1, 0, &location, &len, &st);
    ASSERT_EQ(status_$mst_no_rights, st);
    prot_status = status_$insufficient_rights_to_perform_operation;
    mst_$lookup_object(&uid, 1, 0, &location, &len, &st);
    ASSERT_EQ(status_$mst_insufficient_rights, st);
    prot_status = status_$acl_wrong_type;
    mst_$lookup_object(&uid, 1, 0, &location, &len, &st);
    ASSERT_EQ(status_$mst_wrong_type, st);
    prot_status = status_$file_object_not_found;
    mst_$lookup_object(&uid, 1, 0, &location, &len, &st);
    ASSERT_EQ(status_$mst_object_not_found, st);
    prot_status = 0x00230007;
    mst_$lookup_object(&uid, 1, 0, &location, &len, &st);
    ASSERT_EQ(0x00230007, st);
}

int main(void)
{
    printf("mst_$lookup_object tests\n");
    RUN_TEST(zero_uid_not_found);
    RUN_TEST(local_object_checked);
    RUN_TEST(remote_object_location);
    RUN_TEST(remote_install_error_flagged);
    RUN_TEST(attr_errors);
    RUN_TEST(read_only_volume);
    RUN_TEST(rights_status_mapping);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
