/*
 * peb/test/test_load_wcs.c - PEB_$LOAD_WCS (0x00E31FD8) and its helpers
 *
 * Covers, against the disassembly:
 *   - the save-area arm (0x00E31FF2-0x00E320A4): FILE_$CREATE/FILE_$LOCK/
 *     MST_$MAPS argument cells and the FP_$SAVEP store;
 *   - CHECK_ERR (0x00E31EC8): only the LOW word of status is tested, the
 *     68881 branch clears the save flag and FP_$SAVEP and re-vectors 0x2C,
 *     the PEB branch only prints;
 *   - the microcode arm: the WCS page/offset arithmetic of peb_$write_wcs,
 *     the read-back verify that CRASH_SYSTEMs on a mismatch, the
 *     MST_$UNMAP/FILE_$UNLOCK cells, the two MST_$WIRE_AREA calls sharing
 *     one page list, and the final enable sequence (ctl |= 0xD, the
 *     0xFF73FC byte read, PEB_CTL store, wcs_loaded).
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

int __host_intr_disable_count = 0;

#include "peb/peb_internal.h"
#include "file/file.h"
#include "mst/mst.h"
#include "name/name.h"
#include "vfmt/vfmt.h"

/* ---- host storage ------------------------------------------------------ */

MODULE_DATA_DEFINE(peb_globals_t, PEB_$INFO, 0x00E24C78);
uid_t             UID_$NIL;
m68k_ptr_t        FP_$SAVEP;
void             *arch_$vector_table[ARCH_VECTOR_COUNT];
uint16_t          peb_$const_word_1 = 0x0001;   /* peb/init.c's 0x00E31DCE cell */

static uint8_t    host_ctl_page[0x400];          /* 0xFF7000..0xFF73FF */
/* The PEB control page stands in for SAU2_PEB_CTL (arch/m68k/sau2/hw.h). */
#define SAU2_PEB_CTL peb_ctl_reg
static volatile uint16_t *peb_ctl_reg = (volatile uint16_t *)host_ctl_page;
static uint8_t    host_wcs[0x400];               /* one WCS page, 128 x 8 */
#define SAU2_PEB_CS_PAGE peb_wcs_base
static volatile uint16_t *peb_wcs_base = (volatile uint16_t *)host_wcs;

/* ---- mocks --------------------------------------------------------------- */

static int      create_calls;
static status_$t create_status;
static int      lock_calls;
static const uint16_t *lock_index_arg, *lock_mode_arg;
static const uint8_t *lock_rights_arg;
static status_$t lock_status;
static int      unlock_calls;
static uint16_t *unlock_mode_arg;
static int      maps_calls;
static int16_t  maps_asid; static boolean maps_dir, maps_access;
static uint32_t maps_start, maps_len, maps_size; static int16_t maps_area;
static uint8_t  maps_arena[0x8000];
static int      map_calls;
static uint32_t map_start, map_len, map_extend; static uint16_t map_mode; static uint8_t map_concur;
static uint8_t  map_arena[0x100];
static status_$t map_status;
static int      unmap_calls; static uint32_t unmap_va;
static int      wire_calls;
static uint32_t wire_start[2], wire_end[2]; static void *wire_list[2];
static uint16_t wire_max[2], wire_ret[2];
static int      resolve_calls; static int16_t resolve_len; static status_$t resolve_status;
static int      vfmt_calls; static const char *vfmt_fmt[8];
static int      crash_calls; static const status_$t *crash_arg;

void FILE_$CREATE(uid_t *dir_uid, uid_t *file_uid_ret, status_$t *status_ret)
{
    create_calls++;
    (void)dir_uid;
    file_uid_ret->high = 0x11; file_uid_ret->low = 0x22;
    *status_ret = create_status;
}
void FILE_$LOCK(uid_t *file_uid, const uint16_t *lock_index, const uint16_t *lock_mode,
                const uint8_t *rights, void *lock_info, status_$t *status_ret)
{
    lock_calls++; (void)file_uid; (void)lock_info;
    lock_index_arg = lock_index; lock_mode_arg = lock_mode; lock_rights_arg = rights;
    *status_ret = lock_status;
}
void FILE_$UNLOCK(uid_t *file_uid, uint16_t *lock_mode, status_$t *status_ret)
{
    unlock_calls++; (void)file_uid; unlock_mode_arg = lock_mode; *status_ret = 0;
}
void *MST_$MAPS(int16_t asid, boolean direction, uid_t *uid, uint32_t start_va,
                uint32_t length, int16_t area_id, uint32_t area_size,
                boolean access_rights, void *map_info, status_$t *status)
{
    maps_calls++; (void)uid; (void)map_info;
    maps_asid = asid; maps_dir = direction; maps_start = start_va; maps_len = length;
    maps_area = area_id; maps_size = area_size; maps_access = access_rights;
    *status = 0;
    return maps_arena;
}
void *MST_$MAP(uid_t *uid, uint32_t *start_ptr, uint32_t *length_ptr,
               uint16_t *mode_ptr, uint32_t *extend_ptr,
               uint8_t *concur_ptr, void *map_info, status_$t *status_ret)
{
    map_calls++; (void)uid; (void)map_info;
    map_start = *start_ptr; map_len = *length_ptr; map_mode = *mode_ptr;
    map_extend = *extend_ptr; map_concur = *concur_ptr;
    *status_ret = map_status;
    return map_arena;
}
void MST_$UNMAP(uid_t *uid, uint32_t *start_ptr, uint32_t *map_info, status_$t *status_ret)
{
    unmap_calls++; (void)uid; (void)map_info; unmap_va = *start_ptr; *status_ret = 0;
}
void MST_$WIRE_AREA(const void *start_va_ptr, const void *end_va_ptr,
                    void *page_list, const void *max_pages_ptr, void *page_count_ret)
{
    int n = wire_calls < 2 ? wire_calls : 1;
    wire_calls++;
    wire_start[n] = *(const uint32_t *)start_va_ptr;
    wire_end[n]   = *(const uint32_t *)end_va_ptr;
    wire_list[n]  = page_list;
    wire_max[n]   = *(const uint16_t *)max_pages_ptr;
    *(uint16_t *)page_count_ret = wire_ret[n];
}
void NAME_$RESOLVE(char *path, int16_t *path_len, uid_t *resolved_uid, status_$t *status_ret)
{
    resolve_calls++; (void)path; resolve_len = *path_len;
    resolved_uid->high = 0x33; resolved_uid->low = 0x44;
    *status_ret = resolve_status;
}
void VFMT_$WRITE10(const char *format, ...)
{
    if (vfmt_calls < 8) vfmt_fmt[vfmt_calls] = format;
    vfmt_calls++;
}
void CRASH_SYSTEM(const status_$t *status_p) { crash_calls++; crash_arg = status_p; }
void FIM_$UII(void) { }

/* ---- code under test ---------------------------------------------------- */

#include "../load_wcs.c"

/* ---- framework ----------------------------------------------------------- */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %-44s ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)
#define ASSERT_EQ(expected, actual) do { \
    unsigned long long _e = (unsigned long long)(expected); \
    unsigned long long _a = (unsigned long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n", _e, _a, __LINE__); \
        tests_failed++; return; } } while (0)
#define ASSERT_PTR_EQ(expected, actual) do { \
    const void *_e = (const void *)(expected); const void *_a = (const void *)(actual); \
    if (_e != _a) { printf("FAILED\n    Expected: %p, Got: %p at line %d\n", _e, _a, __LINE__); \
        tests_failed++; return; } } while (0)

static void reset(void)
{
    memset(&PEB_$INFO, 0, sizeof PEB_$INFO);
    memset(host_ctl_page, 0, sizeof host_ctl_page);
    memset(host_wcs, 0, sizeof host_wcs);
    memset(map_arena, 0, sizeof map_arena);
    memset(arch_$vector_table, 0, sizeof arch_$vector_table);
    FP_$SAVEP = 0;
    create_calls = lock_calls = unlock_calls = maps_calls = map_calls = 0;
    unmap_calls = wire_calls = resolve_calls = vfmt_calls = crash_calls = 0;
    create_status = lock_status = map_status = resolve_status = 0;
    wire_ret[0] = 3; wire_ret[1] = 4;
    ARCH_HOST_VA_BASE = (uintptr_t)maps_arena;   /* VAs are offsets from here */
}

/* The mapped microcode file: header + entries at +4, 8 bytes each. */
static void set_microcode(uint16_t start, uint16_t count, const peb_wcs_entry_t *e)
{
    peb_wcs_header_t *h = (peb_wcs_header_t *)(void *)map_arena;
    uint16_t i;
    h->start_addr = start;
    h->entry_count = count;
    for (i = 0; i < count; i++) {
        memcpy(map_arena + 4 + i * 8, &e[i], 8);
    }
}

/* ---- tests ---------------------------------------------------------------- */

TEST(neither_flag_nor_board_does_nothing)
{
    reset();
    PEB_$LOAD_WCS();
    ASSERT_EQ(0, create_calls);
    ASSERT_EQ(0, resolve_calls);
    ASSERT_EQ(0, vfmt_calls);
}

TEST(save_area_arm_creates_locks_and_maps)
{
    reset();
    PEB_$M68881_SAVE_FLAG = -1;
    PEB_$LOAD_WCS();
    ASSERT_EQ(1, create_calls);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(0, *lock_index_arg);          /* 0x00E322C4 */
    ASSERT_EQ(4, *lock_mode_arg);           /* 0x00E322C6 */
    ASSERT_EQ(0, *lock_rights_arg);         /* 0x00E322C8 */
    ASSERT_EQ(1, maps_calls);
    ASSERT_EQ(0, maps_asid);
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)maps_dir);
    ASSERT_EQ(0, maps_start);
    ASSERT_EQ(0x497A, maps_len);
    ASSERT_EQ(0x16, maps_area);
    ASSERT_EQ(0, maps_size);
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)maps_access);
    ASSERT_EQ(ARCH_PTR_TO_VA(maps_arena), FP_$SAVEP);
    ASSERT_EQ(0, vfmt_calls);
    ASSERT_EQ(0, resolve_calls);            /* the PEB arm is never reached */
}

TEST(savep_flag_alone_selects_save_area_arm)
{
    reset();
    PEB_$SAVEP_FLAG = -1;
    PEB_$INSTALLED = -1;
    PEB_$LOAD_WCS();
    ASSERT_EQ(1, create_calls);
    ASSERT_EQ(0, resolve_calls);
}

TEST(check_err_68881_branch_disables_68881)
{
    reset();
    PEB_$M68881_SAVE_FLAG = -1;
    FP_$SAVEP = 0x1234;
    create_status = 0x00010002;             /* low word non-zero */
    PEB_$LOAD_WCS();
    ASSERT_EQ(1, create_calls);
    ASSERT_EQ(0, lock_calls);               /* bailed after CHECK_ERR */
    ASSERT_EQ(3, vfmt_calls);
    ASSERT_PTR_EQ(peb_$fmt_warning, vfmt_fmt[0]);
    ASSERT_PTR_EQ(peb_$msg_create_file, vfmt_fmt[1]);
    ASSERT_PTR_EQ(peb_$fmt_68881_disabled, vfmt_fmt[2]);
    ASSERT_EQ(0, (uint8_t)PEB_$M68881_SAVE_FLAG);
    ASSERT_EQ(0, FP_$SAVEP);
    ASSERT_PTR_EQ((void *)FIM_$UII, arch_$vector_table[11]);
}

TEST(check_err_ignores_high_word_of_status)
{
    reset();
    PEB_$M68881_SAVE_FLAG = -1;
    create_status = 0x00010000;             /* tst.w of the low word sees zero */
    PEB_$LOAD_WCS();
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(0, vfmt_calls);
}

TEST(check_err_peb_branch_only_prints)
{
    reset();
    PEB_$INSTALLED = -1;
    resolve_status = 5;
    PEB_$LOAD_WCS();
    ASSERT_EQ(1, resolve_calls);
    ASSERT_EQ(0x13, resolve_len);
    ASSERT_EQ(0, lock_calls);
    ASSERT_EQ(3, vfmt_calls);
    ASSERT_PTR_EQ(peb_$msg_resolve, vfmt_fmt[1]);
    ASSERT_PTR_EQ(peb_$fmt_peb_disabled, vfmt_fmt[2]);
    ASSERT_EQ(0xFF, (uint8_t)PEB_$INSTALLED);  /* not cleared */
    ASSERT_EQ(0, (uint8_t)PEB_$WCS_LOADED);
}

TEST(microcode_arm_loads_verifies_wires_enables)
{
    static const peb_wcs_entry_t e[3] = {
        { 0x1111, 0x2222, 0x33334444u },
        { 0x5555, 0x6666, 0x77778888u },
        { 0x9999, 0xAAAA, 0xBBBBCCCCu },
    };
    reset();
    PEB_$INSTALLED = -1;
    PEB_$CTL_SHADOW = 0x0003;
    set_microcode(0x0085, 3, e);            /* page 1, offsets 5..7 */
    PEB_$LOAD_WCS();

    ASSERT_EQ(1, resolve_calls);
    ASSERT_EQ(1, lock_calls);
    ASSERT_PTR_EQ(&peb_$const_word_1, lock_mode_arg);   /* 0x00E31DCE shared cell */
    ASSERT_EQ(1, map_calls);
    ASSERT_EQ(0, map_start);
    ASSERT_EQ(0x00010000u, map_len);        /* 0x00E32300 */
    ASSERT_EQ(6, map_mode);                 /* 0x00E322A2 */
    ASSERT_EQ(0, map_extend);
    ASSERT_EQ(0, map_concur);

    /* WCS bytes: entry i at (addr & 0x7F) * 8 */
    ASSERT_EQ(0, memcmp(host_wcs + 5 * 8, &e[0], 8));
    ASSERT_EQ(0, memcmp(host_wcs + 6 * 8, &e[1], 8));
    ASSERT_EQ(0, memcmp(host_wcs + 7 * 8, &e[2], 8));
    ASSERT_EQ(0, crash_calls);

    ASSERT_EQ(1, unmap_calls);
    ASSERT_EQ(ARCH_PTR_TO_VA(map_arena), unmap_va);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_PTR_EQ(&peb_$const_word_1, unlock_mode_arg);

    ASSERT_EQ(2, wire_calls);
    ASSERT_EQ(0x00E70810u, wire_start[0]);
    ASSERT_EQ(0x00E70A3Eu, wire_end[0]);
    ASSERT_EQ(10, wire_max[0]);
    ASSERT_EQ(0x00E84E80u, wire_start[1]);
    ASSERT_EQ(0x00E854D8u, wire_end[1]);
    ASSERT_EQ(10 - 3, wire_max[1]);         /* 10 - wire_count1 */
    ASSERT_PTR_EQ((uint32_t *)wire_list[0] + 3, wire_list[1]);

    /* enable: page bits from the last WCS access (page 1 -> 0x10), then |= 0xD */
    ASSERT_EQ(0x0013 | 0x000D, PEB_$CTL_SHADOW);
    ASSERT_EQ(PEB_$CTL_SHADOW, PEB_CTL);
    ASSERT_EQ(0xFF, (uint8_t)PEB_$WCS_LOADED);
    ASSERT_EQ(0, vfmt_calls);
}

TEST(wcs_page_bits_replace_bits_4_to_9_only)
{
    static const peb_wcs_entry_t e[1] = { { 1, 2, 3 } };
    reset();
    PEB_$INSTALLED = -1;
    PEB_$CTL_SHADOW = 0xFFFF;
    set_microcode(0x1F80, 1, e);            /* page 0x3F, offset 0 */
    PEB_$LOAD_WCS();
    /* 0xFFFF & 0xFC0F | (0x3F << 4) == 0xFFFF; then |= 0xD */
    ASSERT_EQ(0xFFFF, PEB_$CTL_SHADOW);
    ASSERT_EQ(0, memcmp(host_wcs, &e[0], 8));
    ASSERT_EQ(0, crash_calls);
}

TEST(verify_mismatch_crashes_and_continues)
{
    /* Alias PEB_CTL onto WCS slot 0: the page-select store that precedes
     * every read (0x00E31E86) then clobbers word0 of the entry written at
     * WCS address 0 in the pass before, so the first compare fails and
     * CRASH_SYSTEM gets the 0x00E322FC cell.  Entry 1 (slot 1) survives. */
    static const peb_wcs_entry_t e[2] = {
        { 0x0001, 0x0002, 0x00000003u },
        { 0x0004, 0x0005, 0x00000006u },
    };
    reset();
    PEB_$INSTALLED = -1;
    peb_ctl_reg = (volatile uint16_t *)host_wcs;
    set_microcode(0x0000, 2, e);
    PEB_$LOAD_WCS();
    peb_ctl_reg = (volatile uint16_t *)host_ctl_page;

    ASSERT_EQ(1, crash_calls);
    ASSERT_PTR_EQ(&PEB_WCS_Verify_Failed_Err, crash_arg);
    ASSERT_EQ(0x00240009, *crash_arg);
    /* the routine carries on after the (mocked) crash */
    ASSERT_EQ(1, unmap_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(2, wire_calls);
    ASSERT_EQ(0xFF, (uint8_t)PEB_$WCS_LOADED);
}

TEST(zero_entries_skips_both_loops)
{
    reset();
    PEB_$INSTALLED = -1;
    set_microcode(0x0010, 0, NULL);
    PEB_$LOAD_WCS();
    ASSERT_EQ(0, crash_calls);
    ASSERT_EQ(1, unmap_calls);
    ASSERT_EQ(0xFF, (uint8_t)PEB_$WCS_LOADED);
}

int main(void)
{
    printf("PEB_$LOAD_WCS tests\n");
    RUN_TEST(neither_flag_nor_board_does_nothing);
    RUN_TEST(save_area_arm_creates_locks_and_maps);
    RUN_TEST(savep_flag_alone_selects_save_area_arm);
    RUN_TEST(check_err_68881_branch_disables_68881);
    RUN_TEST(check_err_ignores_high_word_of_status);
    RUN_TEST(check_err_peb_branch_only_prints);
    RUN_TEST(microcode_arm_loads_verifies_wires_enables);
    RUN_TEST(wcs_page_bits_replace_bits_4_to_9_only);
    RUN_TEST(verify_mismatch_crashes_and_continues);
    RUN_TEST(zero_entries_skips_both_loops);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
