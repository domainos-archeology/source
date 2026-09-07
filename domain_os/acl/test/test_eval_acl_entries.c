/*
 * acl/test/test_eval_acl_entries.c - unit tests for acl_$eval_acl_entries
 * (0x00E46172)
 *
 * The real acl/eval_acl_entries.c is #included at the bottom and driven with
 * hand-built ACL images.  Every test names the instructions it pins down.
 *
 * The routine calls nothing, so the only mock needed is UID_$NIL.
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

uid_t UID_$NIL = { 0, 0 };

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

#define PERSON_HI 0x11110000u
#define GROUP_HI  0x22220000u
#define ORG_HI    0x33330000u

static acl_$cache_slot_t  slot;
static acl_sid_block_t    sids;
static uid_t              proj[ACL_MAX_PROJECTS];
static acl_$prot_data_t   prot;

static uid_t mk(uint32_t hi, uint32_t lo)
{
    uid_t u;
    u.high = hi;
    u.low  = lo;
    return u;
}

static acl_$acl_entry_t *ent(int i)
{
    return ACL_$CACHE_ENTRY(&slot, i);
}

static void reset_world(void)
{
    memset(&slot, 0, sizeof(slot));
    memset(&sids, 0, sizeof(sids));
    memset(proj, 0, sizeof(proj));
    memset(&prot, 0, sizeof(prot));

    sids.user_sid  = mk(PERSON_HI, 1);
    sids.group_sid = mk(GROUP_HI, 1);
    sids.org_sid   = mk(ORG_HI, 1);

    /* Every rights byte carries ACL_RIGHT_IGNORE unless a test clears it, so
     * the protection-record fast paths stay out of the way by default. */
    prot.owner_rights = ACL_RIGHT_IGNORE;
    prot.group_rights = ACL_RIGHT_IGNORE;
    prot.org_rights   = ACL_RIGHT_IGNORE;
    prot.world_rights = 0x07;
    prot.subsys_rights = 0xFF;
}

static uint16_t run(void)
{
    return acl_$eval_acl_entries(&slot, &sids.user_sid, proj, &prot);
}

/* ------------------------------------------------------------------ */
/* 0x00E4617A: the protection record's owner slot                       */
/* ------------------------------------------------------------------ */

TEST(prot_owner_match_returns_owner_rights)
{
    reset_world();
    prot.owner_rights = 0x0D;               /* bit 4 clear */
    prot.owner = sids.user_sid;
    /* `clr.w D0w / move.b (0x18,A0),D0b` at 0x00E46198 - a zero-extended byte */
    ASSERT_EQ(0x0D, run());
}

TEST(prot_owner_ignore_bit_skips_the_fast_path)
{
    reset_world();
    prot.owner_rights = (uint8_t)(ACL_RIGHT_IGNORE | 0x0D);
    prot.owner = sids.user_sid;
    /* `btst.b #0x4,(0x18,A0)` at 0x00E4617E takes the scan instead, which
     * finds nothing and ends at the world rights. */
    ASSERT_EQ(0x07, run());
}

TEST(prot_owner_mismatch_skips_the_fast_path)
{
    reset_world();
    prot.owner_rights = 0x0D;
    prot.owner = mk(0xDEADBEEFu, 0);
    ASSERT_EQ(0x07, run());
}

/* ------------------------------------------------------------------ */
/* Phase 1 (0x00E461A2): the person scan                                */
/* ------------------------------------------------------------------ */

TEST(person_exact_match_returns_subsys_and_entry_rights)
{
    reset_world();
    slot.entry_count = 2;
    ent(1)->person = mk(PERSON_HI, 99);     /* wrong person */
    ent(1)->rights = 0x00FF;
    ent(2)->person = sids.user_sid;
    ent(2)->group  = sids.group_sid;
    ent(2)->org    = sids.org_sid;
    ent(2)->rights = 0x003C;
    prot.subsys_rights = 0x1F;
    /* 0x00E46224: `move.b (0x1c,A2),D0b` then `and.w (0x1a,A0),D0w` */
    ASSERT_EQ(0x003C & 0x1F, run());
}

TEST(person_match_accepts_nil_group_wildcard)
{
    reset_world();
    slot.entry_count = 1;
    ent(1)->person = sids.user_sid;
    ent(1)->group  = UID_$NIL;              /* 0x00E461E4 wildcard */
    ent(1)->org    = sids.org_sid;
    ent(1)->rights = 0x0011;
    ASSERT_EQ(0x0011, run());
}

TEST(person_match_accepts_nil_org_wildcard)
{
    reset_world();
    slot.entry_count = 1;
    ent(1)->person = sids.user_sid;
    ent(1)->group  = sids.group_sid;
    ent(1)->org    = UID_$NIL;              /* 0x00E4620A wildcard */
    ent(1)->rights = 0x0022;
    ASSERT_EQ(0x0022, run());
}

TEST(person_match_rejects_wrong_group)
{
    reset_world();
    slot.entry_count = 1;
    ent(1)->person = sids.user_sid;
    ent(1)->group  = mk(GROUP_HI, 77);
    ent(1)->org    = sids.org_sid;
    ent(1)->rights = 0x0022;
    /* Neither an exact match nor NIL -> `bne 0x00e46230`, next entry. */
    ASSERT_EQ(0x07, run());
}

TEST(person_scan_stops_at_the_first_nil_person)
{
    reset_world();
    slot.entry_count = 3;
    ent(1)->person = mk(PERSON_HI, 5);
    ent(2)->person = UID_$NIL;              /* 0x00E4623A ends phase 1 */
    ent(3)->person = sids.user_sid;         /* never reached */
    ent(3)->group  = sids.group_sid;
    ent(3)->org    = sids.org_sid;
    ent(3)->rights = 0x00F0;
    ASSERT_EQ(0x07, run());
}

/* ------------------------------------------------------------------ */
/* Phase 2 (0x00E46250): group and project passes                       */
/* ------------------------------------------------------------------ */

TEST(prot_group_match_on_pass_zero)
{
    reset_world();
    prot.group_rights = 0x06;
    prot.group = sids.group_sid;
    /* 0x00E462C6 `or.w D5w,D1w` + 0x00E462C8 `st` -> phase 3 is skipped */
    ASSERT_EQ(0x06, run());
}

TEST(group_entry_match_uses_local_sids)
{
    reset_world();
    slot.entry_count = 2;
    ent(1)->person = UID_$NIL;              /* phase 1 stops immediately */
    ent(1)->group  = sids.group_sid;
    ent(1)->org    = sids.org_sid;
    ent(1)->rights = 0x00AA;
    prot.subsys_rights = 0x0F;
    ASSERT_EQ(0x00AA & 0x0F, run());
}

TEST(group_entry_match_accepts_nil_org)
{
    reset_world();
    slot.entry_count = 1;
    ent(1)->person = UID_$NIL;
    ent(1)->group  = sids.group_sid;
    ent(1)->org    = UID_$NIL;              /* 0x00E46308 wildcard */
    ent(1)->rights = 0x0003;
    ASSERT_EQ(0x0003, run());
}

TEST(project_uid_is_substituted_for_the_group_sid)
{
    reset_world();
    proj[0] = mk(0x44440000u, 7);
    slot.entry_count = 1;
    ent(1)->person = UID_$NIL;
    ent(1)->group  = proj[0];               /* only pass 1 can match it */
    ent(1)->org    = sids.org_sid;
    ent(1)->rights = 0x0005;
    ASSERT_EQ(0x0005, run());
}

TEST(rights_accumulate_across_project_passes)
{
    reset_world();
    proj[0] = mk(0x44440000u, 7);
    proj[1] = mk(0x55550000u, 8);
    slot.entry_count = 3;
    ent(1)->person = UID_$NIL;
    ent(1)->group  = sids.group_sid;
    ent(1)->org    = sids.org_sid;
    ent(1)->rights = 0x0001;
    ent(2)->person = UID_$NIL;
    ent(2)->group  = proj[0];
    ent(2)->org    = sids.org_sid;
    ent(2)->rights = 0x0002;
    ent(3)->person = UID_$NIL;
    ent(3)->group  = proj[1];
    ent(3)->org    = sids.org_sid;
    ent(3)->rights = 0x0004;
    /* Each of the three passes contributes one bit through `or.w D5w,D1w`. */
    ASSERT_EQ(0x0007, run());
}

TEST(nil_project_uid_ends_the_project_loop)
{
    reset_world();
    proj[0] = mk(0x44440000u, 7);
    /* proj[1] stays NIL -> `beq.w 0x00e4635a` at 0x00E4628E */
    slot.entry_count = 2;
    ent(1)->person = UID_$NIL;
    ent(1)->group  = proj[0];
    ent(1)->org    = sids.org_sid;
    ent(1)->rights = 0x0002;
    ent(2)->person = UID_$NIL;
    ent(2)->group  = mk(0x55550000u, 8);    /* would be proj[1] */
    ent(2)->org    = sids.org_sid;
    ent(2)->rights = 0x0004;
    ASSERT_EQ(0x0002, run());
}

TEST(group_scan_stops_at_the_first_nil_group)
{
    reset_world();
    slot.entry_count = 3;
    ent(1)->person = UID_$NIL;
    ent(1)->group  = UID_$NIL;              /* 0x00E46338 ends the scan */
    ent(2)->person = UID_$NIL;
    ent(2)->group  = sids.group_sid;
    ent(2)->org    = sids.org_sid;
    ent(2)->rights = 0x00F0;
    ASSERT_EQ(0x07, run());
}

TEST(prot_group_ignore_bit_falls_through_to_the_entry_scan)
{
    reset_world();
    prot.group_rights = (uint8_t)(ACL_RIGHT_IGNORE | 0x06);
    prot.group = sids.group_sid;
    slot.entry_count = 1;
    ent(1)->person = UID_$NIL;
    ent(1)->group  = sids.group_sid;
    ent(1)->org    = sids.org_sid;
    ent(1)->rights = 0x0009;
    /* `btst.b #0x4,(0x19,A3)` at 0x00E462A2 */
    ASSERT_EQ(0x0009, run());
}

/* ------------------------------------------------------------------ */
/* Phase 3 (0x00E46364): org record, org entries, world                 */
/* ------------------------------------------------------------------ */

TEST(prot_org_match_returns_org_rights)
{
    reset_world();
    prot.org_rights = 0x0B;
    prot.org = sids.org_sid;
    /* 0x00E4638C `move.b (0x1a,A3),D0b` */
    ASSERT_EQ(0x0B, run());
}

TEST(prot_org_ignore_bit_falls_through)
{
    reset_world();
    prot.org_rights = (uint8_t)(ACL_RIGHT_IGNORE | 0x0B);
    prot.org = sids.org_sid;
    ASSERT_EQ(0x07, run());
}

TEST(org_entry_scan_reuses_the_phase_one_match_block)
{
    reset_world();
    slot.entry_count = 2;
    ent(1)->person = UID_$NIL;
    ent(1)->group  = UID_$NIL;              /* phase 2 stops at entry 1 */
    ent(1)->org    = sids.org_sid;
    ent(1)->rights = 0x00CC;
    prot.subsys_rights = 0x0F;
    /* Phase 3 restarts at max_index (1) and 0x00E463AE branches back to the
     * shared 0x00E4621E block. */
    ASSERT_EQ(0x00CC & 0x0F, run());
}

TEST(org_scan_stops_at_the_first_nil_org)
{
    reset_world();
    slot.entry_count = 2;
    ent(1)->person = UID_$NIL;
    ent(1)->group  = UID_$NIL;
    ent(1)->org    = UID_$NIL;              /* 0x00E463BC ends the scan */
    ent(2)->person = UID_$NIL;
    ent(2)->group  = UID_$NIL;
    ent(2)->org    = sids.org_sid;
    ent(2)->rights = 0x00CC;
    ASSERT_EQ(0x07, run());
}

TEST(empty_image_returns_world_rights)
{
    reset_world();
    slot.entry_count = 0;
    prot.world_rights = 0x35;
    /* 0x00E463D6 `move.b (0x1b,A0),D0b` */
    ASSERT_EQ(0x35, run());
}

TEST(subsys_rights_mask_is_applied_to_entry_rights)
{
    reset_world();
    slot.entry_count = 1;
    ent(1)->person = sids.user_sid;
    ent(1)->group  = sids.group_sid;
    ent(1)->org    = sids.org_sid;
    ent(1)->rights = 0xFFFF;
    prot.subsys_rights = 0x2A;
    /* `clr.w D0w / move.b` makes the mask a zero-extended BYTE, so the high
     * half of a 16-bit entry rights word can never survive. */
    ASSERT_EQ(0x002A, run());
}

int main(void)
{
    printf("acl_$eval_acl_entries (0x00E46172)\n");

    RUN_TEST(prot_owner_match_returns_owner_rights);
    RUN_TEST(prot_owner_ignore_bit_skips_the_fast_path);
    RUN_TEST(prot_owner_mismatch_skips_the_fast_path);

    RUN_TEST(person_exact_match_returns_subsys_and_entry_rights);
    RUN_TEST(person_match_accepts_nil_group_wildcard);
    RUN_TEST(person_match_accepts_nil_org_wildcard);
    RUN_TEST(person_match_rejects_wrong_group);
    RUN_TEST(person_scan_stops_at_the_first_nil_person);

    RUN_TEST(prot_group_match_on_pass_zero);
    RUN_TEST(group_entry_match_uses_local_sids);
    RUN_TEST(group_entry_match_accepts_nil_org);
    RUN_TEST(project_uid_is_substituted_for_the_group_sid);
    RUN_TEST(rights_accumulate_across_project_passes);
    RUN_TEST(nil_project_uid_ends_the_project_loop);
    RUN_TEST(group_scan_stops_at_the_first_nil_group);
    RUN_TEST(prot_group_ignore_bit_falls_through_to_the_entry_scan);

    RUN_TEST(prot_org_match_returns_org_rights);
    RUN_TEST(prot_org_ignore_bit_falls_through);
    RUN_TEST(org_entry_scan_reuses_the_phase_one_match_block);
    RUN_TEST(org_scan_stops_at_the_first_nil_org);
    RUN_TEST(empty_image_returns_world_rights);
    RUN_TEST(subsys_rights_mask_is_applied_to_entry_rights);

    printf("\n%d tests, %d failed\n", tests_passed + tests_failed, tests_failed);
    return tests_failed != 0;
}

#include "../eval_acl_entries.c"
