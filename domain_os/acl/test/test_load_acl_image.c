/*
 * acl/test/test_load_acl_image.c - unit tests for acl_$load_acl_image
 * (0x00E45A60) and the two helpers it calls, acl_$expand_default_acl
 * (0x00E45984) and acl_$alloc_cache_slot (0x00E458E4).
 *
 * The real acl/load_acl_image.c, acl/expand_default_acl.c,
 * acl/alloc_cache_slot.c and acl/cache_list.c are #included at the bottom, so
 * the slot allocation, the free/LRU/hash relinking and the version fixups
 * under test are the real code.  Everything outside acl/ - MST_$MAPS,
 * MST_$UNMAP_PRIVI, the FIM cleanup trio, UID_$HASH, ACL_$DEF_ACLDATA and
 * CRASH_SYSTEM - is mocked, as are the two ACL routines that have not been
 * emitted yet (acl_$convert_rights, acl_$convert_image).
 */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>

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
#include "mst/mst.h"      /* status_$mst_object_not_found */

/* ------------------------------------------------------------------ */
/* Globals the module owns                                              */
/* ------------------------------------------------------------------ */

uid_t UID_$NIL = { 0, 0 };

/* Raw bytes at 0x00E17384 / 0x00E174C4 / 0x00E17444 / 0x00E1744C. */
uid_t ACL_$NIL      = { 0x00000100u, 0 };
uid_t ACL_$FNDWRX   = { 0x0001800Fu, 0 };
uid_t ACL_$FILE_ACL = { 0x00000601u, 0 };
uid_t ACL_$DIR_ACL  = { 0x00000600u, 0 };

acl_$cache_slot_t ACL_$ACL_CACHE[ACL_CACHE_SLOTS];
acl_$cache_slot_t ACL_$IMAGE_BUF;
acl_$cache_dir_t  ACL_$CACHE_DIR[ACL_CACHE_SLOTS];
acl_$cache_link_t ACL_$CACHE_LRU_LINKS[ACL_CACHE_LINK_SLOTS];
acl_$cache_link_t ACL_$CACHE_HASH_LINKS[ACL_CACHE_LINK_SLOTS];
int16_t ACL_$CACHE_HASH_BUCKETS_TAB[ACL_CACHE_HASH_BUCKETS];
int16_t ACL_$CACHE_FREE_HEAD;
int16_t ACL_$CACHE_LRU_HEAD;
int16_t ACL_$SUPER_COUNT[PROC1_MAX_PROCESSES];

uint16_t PROC1_$CURRENT = 3;
uint16_t PROC1_$AS_ID   = 9;

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

static jmp_buf crash_jmp;
static int       crash_taken;
static status_$t crash_status;

void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_taken  = 1;
    crash_status = *status_p;
    longjmp(crash_jmp, 1);
}

/* The image MST_$MAPS pretends to have mapped. */
static acl_$cache_slot_t mapped_image;
static int       maps_calls;
static status_$t maps_status;
static int16_t   maps_super_count_seen;    /* ACL_$SUPER_COUNT during the map */
static int16_t   maps_mode_seen;
static int16_t   maps_flags_seen;
static uint32_t  maps_length_seen;
static int16_t   maps_prot_seen;

void *MST_$MAPS(int16_t mode, int16_t flags, uid_t *uid, uint32_t offset,
                uint32_t length, int16_t prot, uint32_t hint, int8_t create,
                void *out, status_$t *status)
{
    (void)uid; (void)offset; (void)hint; (void)create; (void)out;
    maps_calls++;
    maps_mode_seen   = mode;
    maps_flags_seen  = flags;
    maps_length_seen = length;
    maps_prot_seen   = prot;
    maps_super_count_seen = ACL_$SUPER_COUNT[PROC1_$CURRENT];
    *status = maps_status;
    return &mapped_image;
}

static int       unmap_calls;
static status_$t unmap_status;
static uint32_t  unmap_size_seen;
static uint16_t  unmap_asid_seen;
static int16_t   unmap_mode_seen;

void MST_$UNMAP_PRIVI(int16_t mode, uid_t *uid, uint32_t start, uint32_t size,
                      uint16_t asid, status_$t *status_ret)
{
    (void)uid; (void)start;
    unmap_calls++;
    unmap_mode_seen = mode;
    unmap_size_seen = size;
    unmap_asid_seen = asid;
    *status_ret = unmap_status;
}

static int       cleanup_calls;
static int       rls_cleanup_calls;
static int       pop_signal_calls;
static status_$t cleanup_result;

status_$t FIM_$CLEANUP(void *handler) { (void)handler; cleanup_calls++;
                                        return cleanup_result; }
void FIM_$RLS_CLEANUP(void *d)        { (void)d; rls_cleanup_calls++; }
void FIM_$POP_SIGNAL(void *d)         { (void)d; pop_signal_calls++; }

static uint16_t mock_hash;
static uint16_t mock_hash_modulus_seen;

uint32_t UID_$HASH(uid_t *uid, uint16_t *table_size)
{
    (void)uid;
    mock_hash_modulus_seen = *table_size;
    /* Only the low word of D0 is used; put litter in the high half. */
    return 0xBEEF0000u | mock_hash;
}

static int mock_def_acldata_calls;

void ACL_$DEF_ACLDATA(void *acl_data_out, void *uid_out)
{
    acl_$prot_data_t *p = (acl_$prot_data_t *)acl_data_out;
    uid_t            *u = (uid_t *)uid_out;

    mock_def_acldata_calls++;
    memset(p, 0, sizeof(*p));
    p->owner_rights = 0x10;
    p->group_rights = 0x10;
    p->org_rights   = 0x10;
    p->world_rights = 0x0F;
    /* The real routine stores UID_$NIL through its second argument
     * (0x00E47948). */
    *u = UID_$NIL;
}

static uint32_t convert_rights_arg;
static uid_t    convert_rights_type;
static uint8_t  convert_rights_result;

uint8_t acl_$convert_rights(uint32_t old_rights, uid_t *acl_type_uid)
{
    convert_rights_arg  = old_rights;
    convert_rights_type = *acl_type_uid;
    return convert_rights_result;
}

static int      convert_image_calls;
static uint16_t convert_image_length;   /* what the mock reports back */
static uid_t    convert_image_required;
static uid_t    convert_image_subsys;
/* A verbatim copy of the image acl_$load_acl_image handed over, i.e. the
 * result of every version-3/4 fixup. */
static acl_$cache_slot_t convert_image_src;

void acl_$convert_image(acl_$cache_slot_t *src, acl_$prot_data_t *prot,
                        acl_$cache_slot_t *dst, uint16_t *length_ret,
                        status_$t *status_ret)
{
    convert_image_calls++;
    convert_image_src = *src;

    memset(dst, 0, sizeof(*dst));
    dst->version      = 5;
    dst->required_uid = convert_image_required;
    dst->subsys_uid   = convert_image_subsys;
    /* A marker the caller's 0x400-byte copy has to carry into the cache. */
    dst->entries[0] = 0xA5;

    prot->world_rights  = 0x33;
    prot->subsys_rights = 0x44;

    *length_ret = convert_image_length;
    *status_ret = status_$ok;
}

/* ------------------------------------------------------------------ */
/* Fixtures                                                             */
/* ------------------------------------------------------------------ */

static uid_t            acl_uid;
static acl_$prot_data_t prot;
static int8_t           cached_flag;
static status_$t        status;

static void reset_world(void)
{
    int i;

    memset(ACL_$ACL_CACHE, 0, sizeof(ACL_$ACL_CACHE));
    memset(&ACL_$IMAGE_BUF, 0, sizeof(ACL_$IMAGE_BUF));
    memset(ACL_$CACHE_DIR, 0, sizeof(ACL_$CACHE_DIR));
    memset(ACL_$CACHE_LRU_LINKS, 0, sizeof(ACL_$CACHE_LRU_LINKS));
    memset(ACL_$CACHE_HASH_LINKS, 0, sizeof(ACL_$CACHE_HASH_LINKS));
    memset(ACL_$SUPER_COUNT, 0, sizeof(ACL_$SUPER_COUNT));
    memset(&mapped_image, 0, sizeof(mapped_image));
    memset(&convert_image_src, 0, sizeof(convert_image_src));
    memset(&prot, 0, sizeof(prot));

    for (i = 0; i < ACL_CACHE_HASH_BUCKETS; i++) {
        ACL_$CACHE_HASH_BUCKETS_TAB[i] = ACL_CACHE_NO_SLOT;
    }
    ACL_$CACHE_FREE_HEAD = ACL_CACHE_NO_SLOT;
    ACL_$CACHE_LRU_HEAD  = ACL_CACHE_NO_SLOT;

    crash_taken       = 0;
    crash_status      = 0;
    maps_calls        = 0;
    maps_status       = status_$ok;
    maps_super_count_seen = -1;
    unmap_calls       = 0;
    unmap_status      = status_$ok;
    cleanup_calls     = 0;
    rls_cleanup_calls = 0;
    pop_signal_calls  = 0;
    cleanup_result    = status_$cleanup_handler_set;
    mock_hash         = 5;
    mock_def_acldata_calls = 0;
    convert_rights_result  = 0x07;
    convert_rights_arg     = 0;
    convert_image_calls    = 0;
    convert_image_length   = 0x34 + 0x20;
    convert_image_required = UID_$NIL;
    convert_image_subsys   = UID_$NIL;

    /* One free slot, number 4. */
    acl_$cache_list_insert(&ACL_$CACHE_FREE_HEAD, ACL_$CACHE_HASH_LINKS, 4);

    acl_uid.high = 0x11223344u;     /* high word 0x1122 - not a default ACL */
    acl_uid.low  = 0x55667788u;
    cached_flag  = 0x5A;
    status       = 0x7FFFFFFFu;

    /* A ready-made version-5 file image in the mapped object. */
    mapped_image.version  = 5;
    mapped_image.type_uid = ACL_$FILE_ACL;
    ((uint8_t *)&mapped_image)[ACL_CACHE_SLOT_SIZE - 1] = 0x77;
}

static int16_t run(void)
{
    if (setjmp(crash_jmp) != 0) {
        return ACL_CACHE_NO_SLOT;
    }
    return acl_$load_acl_image(&acl_uid, &cached_flag, &prot, &status);
}

/* ------------------------------------------------------------------ */
/* acl_$expand_default_acl (0x00E45984)                                 */
/* ------------------------------------------------------------------ */

TEST(default_acl_uid_short_circuits_the_whole_load)
{
    int16_t r;

    reset_world();
    /* Type word 2 = a directory default ACL; rights 0x0003, bit 13 clear. */
    acl_uid.high = 0x00020003u;
    acl_uid.low  = 0;
    convert_rights_result = 0x0D;

    r = run();

    ASSERT_EQ((uint16_t)ACL_CACHE_NO_SLOT, (uint16_t)r);
    ASSERT_EQ(0xFF, (uint8_t)cached_flag);          /* `st (A0)` */
    ASSERT_EQ(0, maps_calls);                       /* nothing was mapped */
    ASSERT_EQ(4, ACL_$CACHE_FREE_HEAD);             /* no slot was taken */
    ASSERT_EQ(1, mock_def_acldata_calls);
    ASSERT_EQ(0x0D, prot.world_rights);
    /* rights 0x0003 | 0x1E0 (directory), masked with 0x3FFF, bit 25 set. */
    ASSERT_EQ(0x020001E3u, convert_rights_arg);
    ASSERT_EQ(ACL_$DIR_ACL.high, convert_rights_type.high);
    /* ACL_$DEF_ACLDATA's out parameter is the caller's own UID. */
    ASSERT_EQ(0, acl_uid.high);
    ASSERT_EQ(0, acl_uid.low);
}

TEST(literal_rights_bit_is_cleared_instead_of_defaulted)
{
    reset_world();
    /* Type 2 with bit 13 set: the 0x1E0 default must NOT be OR'd in. */
    acl_uid.high = 0x00022003u;
    acl_uid.low  = 0;

    (void)run();

    ASSERT_EQ(0x02000003u, convert_rights_arg);
}

TEST(acl_nil_is_expanded_as_fndwrx)
{
    reset_world();
    acl_uid = ACL_$NIL;

    (void)run();

    ASSERT_EQ(0, maps_calls);
    /* ACL_$FNDWRX = 0x0001800F: type 1 (file), rights 0x800F, bit 13 clear
     * and the type is not 2, so nothing is OR'd in; 0x800F & 0x3FFF = 0x000F. */
    ASSERT_EQ(0x0200000Fu, convert_rights_arg);
    ASSERT_EQ(ACL_$FILE_ACL.high, convert_rights_type.high);
}

TEST(a_non_default_type_word_falls_through_to_the_map)
{
    reset_world();
    acl_uid.high = 0x00030000u;     /* type 3: neither file nor directory */

    (void)run();

    ASSERT_EQ(1, maps_calls);
    ASSERT_EQ(0, mock_def_acldata_calls);
}

/* ------------------------------------------------------------------ */
/* acl_$alloc_cache_slot (0x00E458E4)                                   */
/* ------------------------------------------------------------------ */

TEST(the_free_list_head_is_handed_out_first)
{
    status_$t st = 0x0BADF00Du;
    int16_t   s;

    reset_world();
    acl_$cache_list_insert(&ACL_$CACHE_FREE_HEAD, ACL_$CACHE_HASH_LINKS, 9);

    s = acl_$alloc_cache_slot(&st);

    ASSERT_EQ(9, s);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(4, ACL_$CACHE_FREE_HEAD);
}

TEST(an_empty_free_list_evicts_the_lru_tail)
{
    status_$t st = 0;
    int16_t   s;

    reset_world();
    ACL_$CACHE_FREE_HEAD = ACL_CACHE_NO_SLOT;
    memset(ACL_$CACHE_HASH_LINKS, 0, sizeof(ACL_$CACHE_HASH_LINKS));

    /* Slot 6 was used first, so it is the LRU list's tail. */
    acl_$cache_list_insert(&ACL_$CACHE_LRU_HEAD, ACL_$CACHE_LRU_LINKS, 6);
    acl_$cache_list_insert(&ACL_$CACHE_LRU_HEAD, ACL_$CACHE_LRU_LINKS, 7);
    ACL_$CACHE_DIR[6].hash_bucket = 12;
    ACL_$CACHE_HASH_BUCKETS_TAB[12] = ACL_CACHE_NO_SLOT;
    acl_$cache_list_insert(&ACL_$CACHE_HASH_BUCKETS_TAB[12],
                           ACL_$CACHE_HASH_LINKS, 6);

    s = acl_$alloc_cache_slot(&st);

    ASSERT_EQ(6, s);
    ASSERT_EQ(7, ACL_$CACHE_LRU_HEAD);
    /* It came off its hash bucket too. */
    ASSERT_EQ((uint16_t)ACL_CACHE_NO_SLOT,
              (uint16_t)ACL_$CACHE_HASH_BUCKETS_TAB[12]);
}

TEST(both_lists_empty_crashes_the_system)
{
    status_$t st = 0;

    reset_world();
    ACL_$CACHE_FREE_HEAD = ACL_CACHE_NO_SLOT;
    ACL_$CACHE_LRU_HEAD  = ACL_CACHE_NO_SLOT;

    if (setjmp(crash_jmp) == 0) {
        (void)acl_$alloc_cache_slot(&st);
    }

    ASSERT_EQ(1, crash_taken);
    ASSERT_EQ(0x00230000u, crash_status);
}

/* ------------------------------------------------------------------ */
/* acl_$load_acl_image - the mapping bracket                            */
/* ------------------------------------------------------------------ */

TEST(the_map_runs_inside_a_super_count_bracket)
{
    reset_world();

    (void)run();

    ASSERT_EQ(1, maps_super_count_seen);
    ASSERT_EQ(0, ACL_$SUPER_COUNT[PROC1_$CURRENT]);
}

TEST(map_arguments_match_the_pushes)
{
    reset_world();

    (void)run();

    ASSERT_EQ(9, (uint16_t)maps_mode_seen);         /* PROC1_$AS_ID */
    ASSERT_EQ(0xFF00, (uint16_t)maps_flags_seen);   /* `st -(SP)` */
    ASSERT_EQ(0x400, maps_length_seen);
    ASSERT_EQ(0x12, maps_prot_seen);
    ASSERT_EQ(1, unmap_mode_seen);
    ASSERT_EQ(0x400, unmap_size_seen);
    ASSERT_EQ(9, unmap_asid_seen);
}

/* ------------------------------------------------------------------ */
/* acl_$load_acl_image - the cached hit that frees its slot again       */
/* ------------------------------------------------------------------ */

TEST(an_empty_converted_image_frees_the_slot_and_nils_the_uid)
{
    int16_t r;

    reset_world();
    mapped_image.version   = 4;
    convert_image_length   = 0x34;
    convert_image_required = UID_$NIL;
    convert_image_subsys   = ACL_$FILE_ACL;

    r = run();

    ASSERT_EQ((uint16_t)ACL_CACHE_NO_SLOT, (uint16_t)r);
    ASSERT_EQ(1, convert_image_calls);
    ASSERT_EQ(0xFF, (uint8_t)cached_flag);
    /* The slot went straight back on the free list ... */
    ASSERT_EQ(4, ACL_$CACHE_FREE_HEAD);
    /* ... and was never registered in the directory or a bucket. */
    ASSERT_EQ(0, ACL_$CACHE_DIR[4].acl_uid.high);
    ASSERT_EQ((uint16_t)ACL_CACHE_NO_SLOT,
              (uint16_t)ACL_$CACHE_HASH_BUCKETS_TAB[5]);
    /* 0x00E45E04: the caller's UID is replaced with UID_$NIL. */
    ASSERT_EQ(0, acl_uid.high);
    ASSERT_EQ(0, acl_uid.low);
    ASSERT_EQ(status_$ok, status);
}

TEST(a_dir_acl_subsys_uid_also_counts_as_empty)
{
    reset_world();
    mapped_image.version = 4;
    convert_image_length = 0x34;
    convert_image_subsys = ACL_$DIR_ACL;

    ASSERT_EQ((uint16_t)ACL_CACHE_NO_SLOT, (uint16_t)run());
    ASSERT_EQ(4, ACL_$CACHE_FREE_HEAD);
}

TEST(a_non_empty_converted_image_is_installed)
{
    reset_world();
    mapped_image.version = 4;
    convert_image_length = 0x34 + 0x20;

    ASSERT_EQ(4, run());
    ASSERT_EQ(0xFF, (uint8_t)cached_flag);
    /* ACL_$IMAGE_BUF, not the mapped image, is what landed in the slot. */
    ASSERT_EQ(5, ACL_$ACL_CACHE[4].version);
    ASSERT_EQ(0xA5, ACL_$ACL_CACHE[4].entries[0]);
    /* cached_flag is negative, so the rights words were captured. */
    ASSERT_EQ(0x33, ACL_$CACHE_DIR[4].world_rights);
    ASSERT_EQ(0x44, ACL_$CACHE_DIR[4].subsys_rights);
}

/* ------------------------------------------------------------------ */
/* acl_$load_acl_image - the failure relink paths                       */
/* ------------------------------------------------------------------ */

TEST(map_object_not_found_becomes_an_acl_status_and_relinks)
{
    int16_t r;

    reset_world();
    maps_status = status_$mst_object_not_found;     /* 0x00040001 */

    r = run();

    ASSERT_EQ(4, r);                                /* the slot is returned ... */
    ASSERT_EQ(status_$acl_object_not_found, status);/* ... but so is a status */
    ASSERT_EQ(4, ACL_$CACHE_FREE_HEAD);             /* back on the free list */
    ASSERT_EQ(0, unmap_calls);
    ASSERT_EQ(0, cleanup_calls);
    /* The bracket still balanced. */
    ASSERT_EQ(0, ACL_$SUPER_COUNT[PROC1_$CURRENT]);
}

TEST(any_other_map_failure_sets_the_fatal_bit_and_relinks)
{
    reset_world();
    maps_status = 0x00040003u;                      /* no space available */

    ASSERT_EQ(4, run());
    ASSERT_EQ(0x80040003u, (uint32_t)status);                 /* `bset.b #0x7,(A0)` */
    ASSERT_EQ(4, ACL_$CACHE_FREE_HEAD);
}

TEST(a_map_status_in_the_high_half_alone_is_not_a_failure)
{
    reset_world();
    /* `tst.w (0x2,A0)` looks at the LOW word only. */
    maps_status = 0x12340000u;

    ASSERT_EQ(4, run());
    ASSERT_EQ(1, cleanup_calls);
    ASSERT_EQ(1, rls_cleanup_calls);
}

TEST(a_fault_during_the_copy_unmaps_pops_and_relinks)
{
    int16_t r;

    reset_world();
    cleanup_result = 0x00120003u;   /* anything but "cleanup handler set" */

    r = run();

    ASSERT_EQ(4, r);
    ASSERT_EQ(0x00120003u, status); /* the fault status is handed back */
    ASSERT_EQ(1, unmap_calls);
    ASSERT_EQ(1, pop_signal_calls);
    ASSERT_EQ(0, rls_cleanup_calls);
    ASSERT_EQ(4, ACL_$CACHE_FREE_HEAD);
    /* Nothing was copied. */
    ASSERT_EQ(0, ACL_$ACL_CACHE[4].version);
}

TEST(an_unmap_failure_after_the_copy_relinks_too)
{
    reset_world();
    unmap_status = 0x00040007u;     /* object is not mapped */

    ASSERT_EQ(4, run());
    ASSERT_EQ(0x80040007u, (uint32_t)status);
    ASSERT_EQ(4, ACL_$CACHE_FREE_HEAD);
    /* The copy did happen before the unmap. */
    ASSERT_EQ(5, ACL_$ACL_CACHE[4].version);
}

/* ------------------------------------------------------------------ */
/* acl_$load_acl_image - the success path                               */
/* ------------------------------------------------------------------ */

TEST(a_version_5_image_is_copied_and_registered)
{
    int16_t r;

    reset_world();
    mock_hash = 17;

    r = run();

    ASSERT_EQ(4, r);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x00, (uint8_t)cached_flag);          /* `clr.b (A0)` */
    ASSERT_EQ(0, convert_image_calls);              /* version 5 needs none */
    ASSERT_EQ(1, rls_cleanup_calls);
    ASSERT_EQ(1, unmap_calls);
    /* The whole 0x400 bytes came across. */
    ASSERT_EQ(5, ACL_$ACL_CACHE[4].version);
    ASSERT_EQ(0x77, ((uint8_t *)&ACL_$ACL_CACHE[4])[ACL_CACHE_SLOT_SIZE - 1]);
    /* Directory entry. */
    ASSERT_EQ(0x11223344u, ACL_$CACHE_DIR[4].acl_uid.high);
    ASSERT_EQ(0x55667788u, ACL_$CACHE_DIR[4].acl_uid.low);
    ASSERT_EQ(0x00, (uint8_t)ACL_$CACHE_DIR[4].cached_flag);
    ASSERT_EQ(17, ACL_$CACHE_DIR[4].hash_bucket);
    ASSERT_EQ(ACL_CACHE_HASH_MOD, mock_hash_modulus_seen);
    /* Linked into bucket 17. */
    ASSERT_EQ(4, ACL_$CACHE_HASH_BUCKETS_TAB[17]);
    /* Taken off the free list. */
    ASSERT_EQ((uint16_t)ACL_CACHE_NO_SLOT, (uint16_t)ACL_$CACHE_FREE_HEAD);
}

TEST(a_zero_cached_flag_leaves_the_rights_words_alone)
{
    reset_world();
    ACL_$CACHE_DIR[4].world_rights  = 0xEEEE;
    ACL_$CACHE_DIR[4].subsys_rights = 0xDDDD;
    prot.world_rights  = 0x01;
    prot.subsys_rights = 0x02;

    ASSERT_EQ(4, run());
    /* cached_flag is 0, so the `bpl` at 0x00E45E34 skips both stores. */
    ASSERT_EQ(0xEEEE, ACL_$CACHE_DIR[4].world_rights);
    ASSERT_EQ(0xDDDD, ACL_$CACHE_DIR[4].subsys_rights);
}

/* ------------------------------------------------------------------ */
/* acl_$load_acl_image - the version-3 fixup                            */
/* ------------------------------------------------------------------ */

TEST(version_3_promotion_fills_in_the_version_4_header)
{
    acl_$cache_slot_t *m = &mapped_image;
    int i;

    reset_world();
    m->version  = 3;
    m->type_uid = ACL_$FILE_ACL;
    m->required_uid.high = 0xDEADBEEFu;
    m->subsys_uid.high   = 0xFEEDFACEu;
    m->world_entry_present  = 0x40;
    m->unused_29  = 0x41;
    m->reserved_22 = 0xFFFFFFFFu;
    m->reserved_26 = 0xFFFF;
    memset(m->reserved_2a, 0xFF, sizeof(m->reserved_2a));
    convert_image_length = 0x34 + 0x20;

    ASSERT_EQ(4, run());

    /* The mapped image itself is untouched: the fixup runs on the copy. */
    ASSERT_EQ(3, m->version);

    /* 0x00E45BFE-0x00E45C4E, as acl_$convert_image received it. */
    ASSERT_EQ(4, convert_image_src.version);
    ASSERT_EQ(ACL_$FILE_ACL.high, convert_image_src.subsys_uid.high);
    ASSERT_EQ(ACL_$FILE_ACL.low,  convert_image_src.subsys_uid.low);
    ASSERT_EQ(0, convert_image_src.required_uid.high);
    ASSERT_EQ(0, convert_image_src.required_uid.low);
    ASSERT_EQ(0, convert_image_src.reserved_22);
    ASSERT_EQ(0, convert_image_src.reserved_26);
    ASSERT_EQ(0, (uint8_t)convert_image_src.world_entry_present);   /* file ACL: cleared */
    ASSERT_EQ(0, (uint8_t)convert_image_src.unused_29);
    for (i = 0; i < 10; i++) {
        ASSERT_EQ(0, convert_image_src.reserved_2a[i]);
    }
}

TEST(a_dir_acl_version_3_image_keeps_world_entry_present_through_the_promotion)
{
    acl_$cache_slot_t *m = &mapped_image;

    reset_world();
    m->version  = 3;
    m->type_uid = ACL_$DIR_ACL;
    m->world_entry_present  = -1;
    convert_image_length = 0x34 + 0x20;

    ASSERT_EQ(4, run());
    /* 0x00E45C24-0x00E45C38: only a non-directory image loses world_entry_present. */
    ASSERT_EQ(0xFF, (uint8_t)convert_image_src.world_entry_present);
    ASSERT_EQ(4, convert_image_src.version);
}



/* ------------------------------------------------------------------ */
/* acl_$load_acl_image - the version-4 directory fixups                 */
/* ------------------------------------------------------------------ */

TEST(unconverted_dir_entries_get_the_default_rights_forced_in)
{
    acl_$cache_slot_t *m = &mapped_image;

    reset_world();
    m->version     = 4;
    m->type_uid    = ACL_$DIR_ACL;
    m->world_entry_present     = -1;            /* skip the required-entry append */
    m->entry_count = 2;
    ACL_$V4_ENTRY(m, 1)->rights = 0x00000003u;      /* bit 29 clear */
    ACL_$V4_ENTRY(m, 2)->rights = ACL_V4_RIGHTS_CONVERTED | 0x5u;
    convert_image_length = 0x34 + 0x20;

    ASSERT_EQ(4, run());

    ASSERT_EQ(2, convert_image_src.entry_count);
    /* 0x00E45C86-0x00E45C90: 0x200001E0 OR'd into the unconverted entry ... */
    ASSERT_EQ(0x200001E3u, ACL_$V4_ENTRY(&convert_image_src, 1)->rights);
    /* ... and the already-converted one is untouched. */
    ASSERT_EQ(ACL_V4_RIGHTS_CONVERTED | 0x5u,
              ACL_$V4_ENTRY(&convert_image_src, 2)->rights);
}

TEST(the_missing_required_entry_is_appended)
{
    acl_$cache_slot_t *m = &mapped_image;
    acl_$v4_entry_t   *e1;

    reset_world();
    m->version     = 4;
    m->type_uid    = ACL_$DIR_ACL;
    m->world_entry_present     = 0;             /* non-negative: run the append */
    m->entry_count = 1;
    e1 = ACL_$V4_ENTRY(m, 1);
    e1->person.high = 0x1111;       /* not the all-nil required entry */
    e1->rights      = ACL_V4_RIGHTS_CONVERTED;
    convert_image_length = 0x34 + 0x20;

    ASSERT_EQ(4, run());
    ASSERT_EQ(2, convert_image_src.entry_count);
    /* 0x00E45D12-0x00E45D5A: the appended entry is all-nil with 0x1E0. */
    ASSERT_EQ(0, ACL_$V4_ENTRY(&convert_image_src, 2)->person.high);
    ASSERT_EQ(0, ACL_$V4_ENTRY(&convert_image_src, 2)->group.high);
    ASSERT_EQ(0, ACL_$V4_ENTRY(&convert_image_src, 2)->org.high);
    ASSERT_EQ(0, ACL_$V4_ENTRY(&convert_image_src, 2)->subsys.high);
    ASSERT_EQ(0, ACL_$V4_ENTRY(&convert_image_src, 2)->reserved_20);
    ASSERT_EQ(0, ACL_$V4_ENTRY(&convert_image_src, 2)->reserved_24);
    ASSERT_EQ(ACL_V4_RIGHTS_DEFAULT,
              ACL_$V4_ENTRY(&convert_image_src, 2)->rights);
}

TEST(an_existing_required_entry_is_not_duplicated)
{
    acl_$cache_slot_t *m = &mapped_image;
    acl_$v4_entry_t   *e1;

    reset_world();
    m->version     = 4;
    m->type_uid    = ACL_$DIR_ACL;
    m->world_entry_present     = 0;
    m->entry_count = 1;
    e1 = ACL_$V4_ENTRY(m, 1);
    memset(e1, 0, sizeof(*e1));     /* all-nil: this IS the required entry */
    e1->rights = ACL_V4_RIGHTS_CONVERTED;
    convert_image_length = 0x34 + 0x20;

    ASSERT_EQ(4, run());
    ASSERT_EQ(1, convert_image_src.entry_count);
}

TEST(a_full_image_is_left_alone)
{
    acl_$cache_slot_t *m = &mapped_image;

    reset_world();
    m->version     = 4;
    m->type_uid    = ACL_$DIR_ACL;
    m->world_entry_present     = 0;
    m->entry_count = ACL_V4_MAX_ENTRIES;    /* `cmpi.w #0x16` + `bge` */
    convert_image_length = 0x34 + 0x20;

    ASSERT_EQ(4, run());
    ASSERT_EQ(ACL_V4_MAX_ENTRIES, convert_image_src.entry_count);
}

TEST(a_file_acl_version_4_image_skips_the_dir_fixups_entirely)
{
    acl_$cache_slot_t *m = &mapped_image;
    acl_$v4_entry_t   *e1;

    reset_world();
    m->version     = 4;
    m->type_uid    = ACL_$FILE_ACL;
    m->world_entry_present     = 0;
    m->entry_count = 1;
    e1 = ACL_$V4_ENTRY(m, 1);
    memset(e1, 0, sizeof(*e1));
    convert_image_length = 0x34 + 0x20;

    ASSERT_EQ(4, run());
    ASSERT_EQ(1, convert_image_src.entry_count);
}

int main(void)
{
    printf("acl_$load_acl_image (0x00E45A60) / acl_$expand_default_acl "
           "(0x00E45984) / acl_$alloc_cache_slot (0x00E458E4)\n");

    RUN_TEST(default_acl_uid_short_circuits_the_whole_load);
    RUN_TEST(literal_rights_bit_is_cleared_instead_of_defaulted);
    RUN_TEST(acl_nil_is_expanded_as_fndwrx);
    RUN_TEST(a_non_default_type_word_falls_through_to_the_map);

    RUN_TEST(the_free_list_head_is_handed_out_first);
    RUN_TEST(an_empty_free_list_evicts_the_lru_tail);
    RUN_TEST(both_lists_empty_crashes_the_system);

    RUN_TEST(the_map_runs_inside_a_super_count_bracket);
    RUN_TEST(map_arguments_match_the_pushes);

    RUN_TEST(an_empty_converted_image_frees_the_slot_and_nils_the_uid);
    RUN_TEST(a_dir_acl_subsys_uid_also_counts_as_empty);
    RUN_TEST(a_non_empty_converted_image_is_installed);

    RUN_TEST(map_object_not_found_becomes_an_acl_status_and_relinks);
    RUN_TEST(any_other_map_failure_sets_the_fatal_bit_and_relinks);
    RUN_TEST(a_map_status_in_the_high_half_alone_is_not_a_failure);
    RUN_TEST(a_fault_during_the_copy_unmaps_pops_and_relinks);
    RUN_TEST(an_unmap_failure_after_the_copy_relinks_too);

    RUN_TEST(a_version_5_image_is_copied_and_registered);
    RUN_TEST(a_zero_cached_flag_leaves_the_rights_words_alone);

    RUN_TEST(version_3_promotion_fills_in_the_version_4_header);
    RUN_TEST(a_dir_acl_version_3_image_keeps_world_entry_present_through_the_promotion);

    RUN_TEST(unconverted_dir_entries_get_the_default_rights_forced_in);
    RUN_TEST(the_missing_required_entry_is_appended);
    RUN_TEST(an_existing_required_entry_is_not_duplicated);
    RUN_TEST(a_full_image_is_left_alone);
    RUN_TEST(a_file_acl_version_4_image_skips_the_dir_fixups_entirely);

    printf("\n%d tests, %d failed\n", tests_passed + tests_failed, tests_failed);
    return tests_failed != 0;
}

#include "../load_acl_image.c"
#include "../expand_default_acl.c"
#include "../alloc_cache_slot.c"
#include "../cache_list.c"
