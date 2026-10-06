/*
 * acl/test/test_prim_create_internal.c - unit tests for
 * acl_$prim_create_internal (0x00E4519C) and its nested helpers.
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

#include "acl/acl_internal.h"
#include "uid/uid.h"

uid_t UID_$NIL = { 0, 0 };
uid_t ACL_$FILE_ACL = { 0x00000602, 0 };
uid_t ACL_$DIR_ACL = { 0x00000601, 0 };

#include "../prim_create_internal.c"

static uint8_t src_page[0x400];
static uint8_t img_page[0x400];
static acl_$prot_data_t prot;
static uid_t type;
static int16_t len;
static status_$t st;

static acl_$cache_slot_t *src_hdr(void) { return (acl_$cache_slot_t *)(void *)src_page; }
static acl_$acl_entry_t *src_ent(int i)
{ return (acl_$acl_entry_t *)(void *)(src_page + 0x34 + (i - 1) * 0x20); }
static acl_$v4_image_hdr_t *img_hdr(void) { return (acl_$v4_image_hdr_t *)(void *)img_page; }
static acl_$v4_entry_t *img_ent(int i)
{ return (acl_$v4_entry_t *)(void *)(img_page + 0x34 + (i - 1) * 0x2C); }

static void reset_state(void)
{
    memset(src_page, 0, sizeof(src_page));
    memset(img_page, 0xEE, sizeof(img_page));
    memset(&prot, 0, sizeof(prot));
    type = ACL_$FILE_ACL;
    len = 0x1234;
    st = 0x5555;
    prot.owner.high = 0x500;
    prot.group.high = 0x600;
    prot.org.high = 0x700;
    prot.owner_rights = 0x07;
    prot.group_rights = 0x05;
    prot.org_rights = 0x01;
    prot.world_rights = 0;
}

static void test_rights_mask_helper(void)
{
    ASSERT_EQ(0x2D00000F, acl_$pci_rights_mask(0x07, &ACL_$FILE_ACL));
    ASSERT_EQ(0x2D000007, acl_$pci_rights_mask(0x47, &ACL_$FILE_ACL));
    ASSERT_EQ(0x3F00007F, acl_$pci_rights_mask(0x0F, &ACL_$DIR_ACL));
    ASSERT_EQ(0x2D000000, acl_$pci_rights_mask(0x47, &UID_$NIL));
    ASSERT_EQ(0x3F00000F, acl_$pci_subsys_rights(&ACL_$FILE_ACL));
    ASSERT_EQ(0x3F00007F, acl_$pci_subsys_rights(&ACL_$DIR_ACL));
    ASSERT_EQ(0x3F000001, acl_$pci_subsys_rights(&UID_$NIL));
}

static void test_uid_ge_helper(void)
{
    uid_t a = { 2, 1 }, b = { 2, 1 }, c = { 1, 9 }, d = { 2, 0 };
    ASSERT_EQ(0xFF, (uint8_t)acl_$pci_uid_ge(&a, &b));
    ASSERT_EQ(0xFF, (uint8_t)acl_$pci_uid_ge(&a, &c));
    ASSERT_EQ(0, acl_$pci_uid_ge(&c, &a));
    ASSERT_EQ(0, acl_$pci_uid_ge(&d, &a));
}

static void test_no_source_entries(void)
{
    acl_$prim_create_internal(&prot, src_page, 0, &type, 0, img_page, &len, &st);
    ASSERT_EQ(0x5555, st);                      /* never written on success */
    ASSERT_EQ(4, img_hdr()->version);
    ASSERT_EQ(0x602, img_hdr()->acl_type.high);
    ASSERT_EQ(0, img_hdr()->required_uid.high);
    ASSERT_EQ(0x602, img_hdr()->subsys_uid.high);
    ASSERT_EQ(3, img_hdr()->entry_count);
    ASSERT_EQ(0x34 + 3 * 0x2C, len);
    ASSERT_EQ(0x500, img_ent(1)->person.high);
    ASSERT_EQ(0x2D00000F, img_ent(1)->rights);
    ASSERT_EQ(0x600, img_ent(2)->group.high);
    ASSERT_EQ(0, img_ent(2)->person.high);
    ASSERT_EQ(0x700, img_ent(3)->org.high);
    ASSERT_EQ(0x2D000009, img_ent(3)->rights);
    ASSERT_EQ(0, img_ent(3)->reserved_24);
    ASSERT_EQ(0xEE, img_page[0x34 + 3 * 0x2C]);
}

static void test_merge_into_runs(void)
{
    /* persons 0x900 and 0x100 (owner 0x500 goes between), one group
     * 0x650 (group 0x600 after it), no org run */
    src_hdr()->entry_count = 3;
    src_hdr()->required_uid.high = 0xAAAA;
    src_hdr()->subsys_uid.high = 0xBBBB;
    src_ent(1)->person.high = 0x900; src_ent(1)->rights = 0x01;
    src_ent(2)->person.high = 0x100; src_ent(2)->rights = 0x02;
    src_ent(3)->group.high = 0x650;  src_ent(3)->rights = 0x04;
    acl_$prim_create_internal(&prot, src_page, 0x80, &type, 0, img_page, &len, &st);
    ASSERT_EQ(0xAAAA, img_hdr()->required_uid.high);
    ASSERT_EQ(0xBBBB, img_hdr()->subsys_uid.high);
    ASSERT_EQ(6, img_hdr()->entry_count);
    ASSERT_EQ(0x900, img_ent(1)->person.high);
    ASSERT_EQ(0x2D000009, img_ent(1)->rights);
    ASSERT_EQ(0, img_ent(1)->subsys.high);
    ASSERT_EQ(0x500, img_ent(2)->person.high);
    ASSERT_EQ(0x100, img_ent(3)->person.high);
    ASSERT_EQ(0x650, img_ent(4)->group.high);
    ASSERT_EQ(0x600, img_ent(5)->group.high);
    ASSERT_EQ(0x700, img_ent(6)->org.high);
}

static void test_marks_and_suppression(void)
{
    prot.owner_rights = 0x27;                   /* marked */
    prot.group_rights = 0x10;                   /* no entry */
    prot.org_rights = 0x21;
    prot.world_rights = 0x13;                   /* bit 4, still appended */
    acl_$prim_create_internal(&prot, src_page, 0, &type, 0, img_page, &len, &st);
    ASSERT_EQ(3, img_hdr()->entry_count);
    ASSERT_EQ(1, img_hdr()->owner_slot);
    ASSERT_EQ(0, img_hdr()->group_slot);
    ASSERT_EQ(2, img_hdr()->org_slot);
    ASSERT_EQ(0, img_ent(3)->person.high);
    ASSERT_EQ(0x2D00000D, img_ent(3)->rights);
}

static void test_flag_gives_subsys_rights(void)
{
    prot.owner_rights = 0x27;
    prot.world_rights = 0x21;
    acl_$prim_create_internal(&prot, src_page, 0, &type, (int8_t)0xFF, img_page,
                              &len, &st);
    ASSERT_EQ(0, img_hdr()->owner_slot);
    ASSERT_EQ(0x3F00000F, img_ent(1)->rights);
    ASSERT_EQ(0x3F00000F, img_ent(4)->rights);
    ASSERT_EQ(4, img_hdr()->entry_count);
}

static void test_nil_owner_not_counted(void)
{
    prot.owner.high = 0;
    acl_$prim_create_internal(&prot, src_page, 0, &type, 0, img_page, &len, &st);
    ASSERT_EQ(2, img_hdr()->entry_count);
    ASSERT_EQ(0x600, img_ent(1)->group.high);   /* overwrote the owner */
}

static void test_acl_full(void)
{
    src_hdr()->entry_count = 20;
    acl_$prim_create_internal(&prot, src_page, 1, &type, 0, img_page, &len, &st);
    ASSERT_EQ(status_$acl_is_full, st);
    ASSERT_EQ(0x1234, len);
    /* with src_len <= 0 the source count is not in the check */
    st = 0;
    acl_$prim_create_internal(&prot, src_page, 0, &type, 0, img_page, &len, &st);
    ASSERT_EQ(0, st);
}

int main(void)
{
    printf("acl_$prim_create_internal tests\n");
    RUN_TEST(rights_mask_helper);
    RUN_TEST(uid_ge_helper);
    RUN_TEST(no_source_entries);
    RUN_TEST(merge_into_runs);
    RUN_TEST(marks_and_suppression);
    RUN_TEST(flag_gives_subsys_rights);
    RUN_TEST(nil_owner_not_counted);
    RUN_TEST(acl_full);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
