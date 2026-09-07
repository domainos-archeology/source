/*
 * acl/test/test_convert_image.c - unit tests for acl_$convert_image
 * (0x00E44E68).
 *
 * The real acl/convert_image.c and acl/convert_rights.c are #included at the
 * bottom, so the header rewrite, the entry walk and the rights conversion
 * under test are the real code.  The only routine outside acl/ that
 * acl_$convert_image calls is ACL_$DEF_ACLDATA, which is mocked here with the
 * values the real one writes (0x00E47916-0x00E47948 plus the constant block at
 * 0x00E47958).
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  %-46s ", #name);                  \
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

#include "acl/acl_internal.h"

/* ------------------------------------------------------------------ */
/* Globals the module owns                                              */
/* ------------------------------------------------------------------ */

uid_t UID_$NIL      = { 0, 0 };
/* Raw bytes at 0x00E17444 / 0x00E1744C. */
uid_t ACL_$FILE_ACL = { 0x00000601u, 0 };
uid_t ACL_$DIR_ACL  = { 0x00000600u, 0 };

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

static int     def_acldata_calls;
static uint8_t def_acldata_subsys_rights;  /* what the mock leaves at +0x1C */

void ACL_$DEF_ACLDATA(void *acl_data_out, void *uid_out)
{
    acl_$prot_data_t *p = (acl_$prot_data_t *)acl_data_out;
    uid_t            *u = (uid_t *)uid_out;

    def_acldata_calls++;
    memset(p, 0, sizeof(*p));
    /* 0x00E47916-0x00E4792C */
    p->owner_rights  = 0x10;
    p->group_rights  = 0x10;
    p->org_rights    = 0x10;
    p->world_rights  = 0x0F;
    /* 0x00E4792E-0x00E47946: the constant block at 0x00E47958 starts with a
     * zero longword, so +0x1C is normally 0.  Configurable so the "OR into
     * whatever was already there" behaviour can be observed. */
    p->subsys_rights = def_acldata_subsys_rights;
    /* 0x00E47948 */
    *u = UID_$NIL;
}

/* ------------------------------------------------------------------ */
/* Fixtures                                                             */
/* ------------------------------------------------------------------ */

static acl_$cache_slot_t src_img;
static acl_$cache_slot_t dst_img;
static acl_$prot_data_t  prot;
static uint16_t          conv_len;
static status_$t         conv_status;

static const uid_t uid_a = { 0x11111111u, 0x22222222u };
static const uid_t uid_b = { 0x33333333u, 0x44444444u };
static const uid_t uid_c = { 0x55555555u, 0x66666666u };
static const uid_t uid_req = { 0x77777777u, 0x88888888u };
static const uid_t uid_sub = { 0x99999999u, 0xAAAAAAAAu };

static void reset(int16_t version, const uid_t *type_uid)
{
    memset(&src_img, 0, sizeof(src_img));
    memset(&dst_img, 0xEE, sizeof(dst_img));
    memset(&prot, 0xCC, sizeof(prot));
    conv_len    = 0xFFFF;
    conv_status = 0xDEADBEEFu;

    def_acldata_calls         = 0;
    def_acldata_subsys_rights = 0;

    src_img.version      = version;
    src_img.type_uid     = *type_uid;
    src_img.required_uid = uid_req;
    src_img.subsys_uid   = uid_sub;
}

/* Fill version-3/4 entry `i` (1-based). */
static void set_entry(int i, const uid_t *person, const uid_t *group,
                      const uid_t *org, uint32_t rights)
{
    acl_$v4_entry_t *e = ACL_$V4_ENTRY(&src_img, i);

    e->person      = *person;
    e->group       = *group;
    e->org         = *org;
    e->subsys      = uid_sub;
    e->reserved_20 = 0x12345678u;
    e->reserved_24 = 0x9ABCDEF0u;
    e->rights      = rights;

    if ((uint16_t)i > src_img.entry_count) {
        src_img.entry_count = (uint16_t)i;
    }
}

/* ------------------------------------------------------------------ */
/* Header rewrite (0x00E44E74-0x00E44EDE)                               */
/* ------------------------------------------------------------------ */

TEST(the_header_is_stamped_version_5_and_the_uids_carry_over)
{
    reset(3, &ACL_$FILE_ACL);
    acl_$convert_image(&src_img, &prot, &dst_img, &conv_len, &conv_status);

    ASSERT_EQ(5, dst_img.version);
    ASSERT_EQ(ACL_$FILE_ACL.high, dst_img.type_uid.high);
    ASSERT_EQ(ACL_$FILE_ACL.low,  dst_img.type_uid.low);
    ASSERT_EQ(uid_req.high, dst_img.required_uid.high);
    ASSERT_EQ(uid_req.low,  dst_img.required_uid.low);
    ASSERT_EQ(uid_sub.high, dst_img.subsys_uid.high);
    ASSERT_EQ(uid_sub.low,  dst_img.subsys_uid.low);
}

TEST(status_is_cleared_and_never_set)
{
    reset(4, &ACL_$DIR_ACL);
    acl_$convert_image(&src_img, &prot, &dst_img, &conv_len, &conv_status);
    ASSERT_EQ(status_$ok, conv_status);
}

TEST(the_header_tail_and_both_flags_are_cleared)
{
    reset(4, &ACL_$FILE_ACL);
    acl_$convert_image(&src_img, &prot, &dst_img, &conv_len, &conv_status);

    /* 0x00E44EB6-0x00E44ECA */
    ASSERT_EQ(0, dst_img.reserved_0a);
    ASSERT_EQ(0, dst_img.reserved_10);
    ASSERT_EQ(0, dst_img.reserved_22);
    ASSERT_EQ(0, dst_img.reserved_26);
    ASSERT_EQ(0, (uint8_t)dst_img.world_entry_present);
    ASSERT_EQ(0, (uint8_t)dst_img.unused_29);
}

TEST(the_twelve_word_clear_runs_into_the_first_entry)
{
    const uint8_t *b = (const uint8_t *)&dst_img;
    int            i;

    /* 0x00E44EA6-0x00E44EB4: twelve words from dst+0x2A, i.e. 0x2A..0x41 -
     * ten bytes of reserved_2a AND the first fourteen bytes of entry 1.  With
     * no entries to copy, the 0xEE fill has to survive at 0x42 and beyond. */
    reset(4, &ACL_$FILE_ACL);
    acl_$convert_image(&src_img, &prot, &dst_img, &conv_len, &conv_status);

    for (i = 0x2A; i <= 0x41; i++) {
        ASSERT_EQ(0x00, b[i]);
    }
    ASSERT_EQ(0xEE, b[0x42]);
}

TEST(def_acldata_is_called_once_and_world_rights_are_then_cleared)
{
    reset(4, &ACL_$FILE_ACL);
    acl_$convert_image(&src_img, &prot, &dst_img, &conv_len, &conv_status);

    ASSERT_EQ(1, def_acldata_calls);
    /* 0x00E44EDC: `clr.b (0x1b,A0)` immediately after the call. */
    ASSERT_EQ(0x00, prot.world_rights);
    ASSERT_EQ(0x10, prot.owner_rights);
}

/* ------------------------------------------------------------------ */
/* The empty image                                                      */
/* ------------------------------------------------------------------ */

TEST(an_empty_image_yields_no_entries_and_length_0x34)
{
    reset(4, &ACL_$DIR_ACL);
    acl_$convert_image(&src_img, &prot, &dst_img, &conv_len, &conv_status);

    /* 0x00E45016-0x00E4503A: entry_index never left 1. */
    ASSERT_EQ(0, dst_img.entry_count);
    ASSERT_EQ(0x34, conv_len);
}

/* ------------------------------------------------------------------ */
/* A version-3 file image                                               */
/* ------------------------------------------------------------------ */

TEST(a_v3_file_image_copies_its_entries_and_converts_the_rights)
{
    acl_$acl_entry_t *e1, *e2;

    reset(3, &ACL_$FILE_ACL);
    /* Old bits 0|1|2|3: file mapping gives 0x07. */
    set_entry(1, &uid_a, &uid_b, &uid_c, 0x0000000Fu);
    /* Old bit 3 alone: file mapping gives 0x00. */
    set_entry(2, &uid_c, &uid_a, &uid_b, ACL_V4_RIGHT_3);

    acl_$convert_image(&src_img, &prot, &dst_img, &conv_len, &conv_status);

    ASSERT_EQ(2, dst_img.entry_count);
    ASSERT_EQ(0x34 + 2 * 0x20, conv_len);

    e1 = ACL_$CACHE_ENTRY(&dst_img, 1);
    e2 = ACL_$CACHE_ENTRY(&dst_img, 2);

    ASSERT_EQ(uid_a.high, e1->person.high);
    ASSERT_EQ(uid_b.high, e1->group.high);
    ASSERT_EQ(uid_c.high, e1->org.high);
    /* 0x00E44F86: the longword store zeroes reserved_18. */
    ASSERT_EQ(0x0000, e1->reserved_18);
    ASSERT_EQ(0x0007, e1->rights);

    ASSERT_EQ(uid_c.high, e2->person.high);
    ASSERT_EQ(0x0000, e2->rights);

    /* prot->subsys_rights collects bits 0, 1, 2, 3 and 6 of every entry. */
    ASSERT_EQ(0x07, prot.subsys_rights);
    /* No all-nil entry, so world_rights kept the value the clear left. */
    ASSERT_EQ(0x00, prot.world_rights);
}

TEST(the_v4_subsys_uid_is_dropped_and_only_24_bytes_are_copied)
{
    acl_$acl_entry_t *e1;
    const uint8_t    *b;

    reset(3, &ACL_$FILE_ACL);
    set_entry(1, &uid_a, &uid_b, &uid_c, 0);
    acl_$convert_image(&src_img, &prot, &dst_img, &conv_len, &conv_status);

    e1 = ACL_$CACHE_ENTRY(&dst_img, 1);
    b  = (const uint8_t *)e1;

    /* 0x00E44F5E-0x00E44F70: `moveq #0x17` + dbf = 24 bytes.  reserved_1c is
     * never written, so it still holds the 0xEE fill. */
    ASSERT_EQ(uid_c.low, e1->org.low);
    ASSERT_EQ(0xEE, b[0x1C]);
    ASSERT_EQ(0xEE, b[0x1F]);
}

/* ------------------------------------------------------------------ */
/* A version-4 directory image                                          */
/* ------------------------------------------------------------------ */

TEST(a_v4_dir_image_uses_the_directory_bit_assignment)
{
    acl_$acl_entry_t *e1;

    reset(4, &ACL_$DIR_ACL);
    /* ACL_V4_RIGHTS_DEFAULT: directory mapping gives 0x41. */
    set_entry(1, &uid_a, &uid_b, &uid_c, ACL_V4_RIGHTS_DEFAULT);

    acl_$convert_image(&src_img, &prot, &dst_img, &conv_len, &conv_status);

    e1 = ACL_$CACHE_ENTRY(&dst_img, 1);
    ASSERT_EQ(1, dst_img.entry_count);
    ASSERT_EQ(0x54, conv_len);
    ASSERT_EQ(0x0041, e1->rights);
    /* Bits 0 and 6 are both in the accumulate set. */
    ASSERT_EQ(0x41, prot.subsys_rights);
}

TEST(the_all_nil_entry_becomes_world_rights_and_is_not_copied)
{
    reset(4, &ACL_$DIR_ACL);
    set_entry(1, &uid_a, &uid_b, &uid_c, ACL_V4_RIGHT_4);
    /* 0x00E44F08-0x00E44F56: person, group and org all UID_$NIL. */
    set_entry(2, &UID_$NIL, &UID_$NIL, &UID_$NIL, ACL_V4_RIGHTS_DEFAULT);
    set_entry(3, &uid_c, &uid_a, &uid_b, ACL_V4_RIGHT_4);

    acl_$convert_image(&src_img, &prot, &dst_img, &conv_len, &conv_status);

    /* Two copied entries, and entry 3 landed in destination slot 2. */
    ASSERT_EQ(2, dst_img.entry_count);
    ASSERT_EQ(0x74, conv_len);
    ASSERT_EQ(uid_c.high, ACL_$CACHE_ENTRY(&dst_img, 2)->person.high);
    /* 0x00E44F56: the world entry's rights, directory mapping. */
    ASSERT_EQ(0x41, prot.world_rights);
    /* The world entry contributes nothing to subsys_rights; the other two
     * entries convert 0x10 to 0x00 under the directory assignment. */
    ASSERT_EQ(0x00, prot.subsys_rights);
}

TEST(subsys_rights_are_or_ed_into_what_def_acldata_left)
{
    reset(4, &ACL_$DIR_ACL);
    def_acldata_subsys_rights = 0x20;   /* not one of the five copied bits */
    set_entry(1, &uid_a, &uid_b, &uid_c, ACL_V4_RIGHTS_DEFAULT);

    acl_$convert_image(&src_img, &prot, &dst_img, &conv_len, &conv_status);
    ASSERT_EQ(0x61, prot.subsys_rights);
}

/* ------------------------------------------------------------------ */
/* Neither ACL type                                                     */
/* ------------------------------------------------------------------ */

TEST(an_unknown_acl_type_accumulates_no_subsystem_rights)
{
    static const uid_t other = { 0x00000602u, 0 };
    acl_$acl_entry_t  *e1;

    reset(4, &other);
    /* Bit 25 is the only bit an unknown type converts, and the whole
     * accumulate block at 0x00E44FB4 is skipped. */
    set_entry(1, &uid_a, &uid_b, &uid_c, ACL_V4_RIGHT_25 | 0x0000000Fu);

    acl_$convert_image(&src_img, &prot, &dst_img, &conv_len, &conv_status);

    e1 = ACL_$CACHE_ENTRY(&dst_img, 1);
    ASSERT_EQ(1, dst_img.entry_count);
    ASSERT_EQ(0x0008, e1->rights);
    ASSERT_EQ(0x00, prot.subsys_rights);
}

/* ------------------------------------------------------------------ */
/* Loop bounds                                                          */
/* ------------------------------------------------------------------ */

TEST(an_image_of_nothing_but_nil_entries_reports_no_entries)
{
    reset(4, &ACL_$FILE_ACL);
    set_entry(1, &UID_$NIL, &UID_$NIL, &UID_$NIL, ACL_V4_RIGHT_3);
    set_entry(2, &UID_$NIL, &UID_$NIL, &UID_$NIL, 0);

    acl_$convert_image(&src_img, &prot, &dst_img, &conv_len, &conv_status);

    /* 0x00E45016: `cmpi.w #0x1,D2w` + `ble` - entry_index never advanced. */
    ASSERT_EQ(0, dst_img.entry_count);
    ASSERT_EQ(0x34, conv_len);
    /* The LAST nil entry wins: rights 0 under the file assignment is 0x40. */
    ASSERT_EQ(0x40, prot.world_rights);
}

TEST(a_negative_entry_count_word_skips_the_loop)
{
    reset(4, &ACL_$FILE_ACL);
    set_entry(1, &uid_a, &uid_b, &uid_c, 0);
    /* 0x00E44EF2-0x00E44EF4: `sub.w D2w,D0w` + `bmi.w` is a SIGNED word test,
     * so an entry_count of 0x8001 (count - 1 = 0x8000) reads as "none". */
    src_img.entry_count = 0x8001;

    acl_$convert_image(&src_img, &prot, &dst_img, &conv_len, &conv_status);

    ASSERT_EQ(0, dst_img.entry_count);
    ASSERT_EQ(0x34, conv_len);
}

int main(void)
{
    printf("acl_$convert_image (0x00E44E68)\n");

    RUN_TEST(the_header_is_stamped_version_5_and_the_uids_carry_over);
    RUN_TEST(status_is_cleared_and_never_set);
    RUN_TEST(the_header_tail_and_both_flags_are_cleared);
    RUN_TEST(the_twelve_word_clear_runs_into_the_first_entry);
    RUN_TEST(def_acldata_is_called_once_and_world_rights_are_then_cleared);

    RUN_TEST(an_empty_image_yields_no_entries_and_length_0x34);

    RUN_TEST(a_v3_file_image_copies_its_entries_and_converts_the_rights);
    RUN_TEST(the_v4_subsys_uid_is_dropped_and_only_24_bytes_are_copied);

    RUN_TEST(a_v4_dir_image_uses_the_directory_bit_assignment);
    RUN_TEST(the_all_nil_entry_becomes_world_rights_and_is_not_copied);
    RUN_TEST(subsys_rights_are_or_ed_into_what_def_acldata_left);

    RUN_TEST(an_unknown_acl_type_accumulates_no_subsystem_rights);

    RUN_TEST(an_image_of_nothing_but_nil_entries_reports_no_entries);
    RUN_TEST(a_negative_entry_count_word_skips_the_loop);

    printf("\n%d tests, %d failed\n", tests_passed + tests_failed, tests_failed);
    return tests_failed != 0;
}

#include "../convert_image.c"
#include "../convert_rights.c"
