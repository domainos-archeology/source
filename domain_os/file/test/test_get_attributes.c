/*
 * file/test/test_get_attributes.c - unit tests for the two attribute-query
 * entry points that hand AST_$GET_ATTRIBUTES an object-location record:
 *
 *   FILE_$GET_ATTRIBUTES  (0x00E5D984)  - full 0x90-byte format
 *   FILE_$GET_ATTR_INFO   (0x00E5D7F4)  - compact 0x7A-byte format
 *
 * The real .c files are #included at the bottom; AST_$GET_ATTRIBUTES and
 * FILE_$DELETE_INT are mocked here.
 *
 * The behaviours exercised are the ones source-x18q found wrong:
 *   - argument 1 is the 0x20-byte object-location record, and the object UID
 *     goes at its +0x08 field, not at its head (0x00E5D9E0 / 0x00E5D856);
 *   - both routines copy the whole 0x20-byte record back out to the caller's
 *     record (0x00E5DA30 / 0x00E5D88E) - the old code wrote the same 8 bytes
 *     four times;
 *   - FILE_$GET_ATTRIBUTES checks the buffer size AFTER the request dispatch
 *     and after seeding the descriptor (0x00E5D9F2), while FILE_$GET_ATTR_INFO
 *     checks it after the AST call and after the record copy-back
 *     (0x00E5D89C);
 *   - the FILE_$DELETE_INT probe reads the UID out of the CALLER's record
 *     (`pea (0x8,A3)` / `pea (0x8,A1)`), not out of file_uid;
 *   - the compact record's field offsets (0x00E5D8B0-0x00E5D978).
 */

#include <stdio.h>
#include <string.h>

#include "file/file_internal.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_failed = 0;
static int tests_run = 0;
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
        unsigned long _e = (unsigned long)(expected);                         \
        unsigned long _a = (unsigned long)(actual);                           \
        if (_e != _a) {                                                       \
            if (current_failed == 0) { printf("FAILED\n"); }                  \
            printf("      line %d: expected 0x%lx, got 0x%lx\n",              \
                   __LINE__, _e, _a);                                         \
            current_failed = 1; tests_failed++;                               \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ============================================================================
 * Mocks
 * ============================================================================ */

/* What the mocked AST_$GET_ATTRIBUTES saw and what it hands back. */
static int             mock_ast_calls;
static uid_t           mock_ast_seen_uid;      /* record+0x08 on entry */
static int8_t          mock_ast_seen_flagbyte; /* record+0x1D on entry */
static uint16_t        mock_ast_seen_flags;
static status_$t       mock_ast_status;
static uint8_t         mock_ast_attrs[AST_ATTR_REC_SIZE];
static file_$obj_loc_t mock_ast_record_out;

void AST_$GET_ATTRIBUTES(file_$obj_loc_t *loc_rec, uint16_t flags, void *attrs,
                         status_$t *status)
{
    mock_ast_calls++;
    mock_ast_seen_uid      = loc_rec->uid;
    mock_ast_seen_flagbyte = loc_rec->flags;
    mock_ast_seen_flags    = flags;

    memcpy(attrs, mock_ast_attrs, AST_ATTR_REC_SIZE);
    *loc_rec = mock_ast_record_out;
    *status  = mock_ast_status;
}

/* What the mocked FILE_$DELETE_INT probe saw and returns. */
static int       mock_delete_calls;
static uid_t     mock_delete_seen_uid;
static int8_t    mock_delete_result;

int8_t FILE_$DELETE_INT(uid_t *file_uid, uint16_t flags, uint8_t *result,
                        status_$t *status_ret)
{
    (void)flags;
    mock_delete_calls++;
    mock_delete_seen_uid = *file_uid;
    result[0] = 0;
    result[1] = 0;
    *status_ret = status_$ok;
    return mock_delete_result;
}

/* Only FILE_$ATTRIBUTES / FILE_$ACT_ATTRIBUTES use this; the two units under
 * test do not, but file_internal.h declares it. */
void VTOCE_$NEW_TO_OLD(void *new_vtoce_ptr, char *flags, void *old_vtoce_ptr)
{
    (void)new_vtoce_ptr; (void)flags; (void)old_vtoce_ptr;
}

/* ============================================================================
 * Fixtures
 * ============================================================================ */

#define CALLER_UID_HIGH     0x11112222u
#define CALLER_UID_LOW      0x33334444u
#define RECORD_UID_HIGH     0xAAAABBBBu
#define RECORD_UID_LOW      0xCCCCDDDDu

static uid_t           caller_uid;
static file_$obj_loc_t caller_record;
static status_$t       status;

static void reset(void)
{
    int i;

    mock_ast_calls = 0;
    mock_delete_calls = 0;
    mock_delete_result = 0;
    mock_ast_status = status_$ok;
    mock_ast_seen_flags = 0xFFFF;
    memset(&mock_ast_seen_uid, 0, sizeof(mock_ast_seen_uid));
    mock_ast_seen_flagbyte = 0;

    for (i = 0; i < AST_ATTR_REC_SIZE; i++) {
        mock_ast_attrs[i] = (uint8_t)i;
    }

    memset(&mock_ast_record_out, 0, sizeof(mock_ast_record_out));
    mock_ast_record_out.reserved_00 = 0x0102;
    mock_ast_record_out.volume      = 0x0304;
    mock_ast_record_out.block_hint  = 0x05060708;
    mock_ast_record_out.uid.high = 0x090A0B0Cu;
    mock_ast_record_out.uid.low  = 0x0D0E0F10u;
    mock_ast_record_out.loc_info = 0x11121314u;
    mock_ast_record_out.node     = 0x15161718u;
    mock_ast_record_out.reserved_18 = 0x191A1B1Cu;
    mock_ast_record_out.rights_bits = 0x1D;
    mock_ast_record_out.flags       = 0x1E;
    mock_ast_record_out.reserved_1e = 0x1F20;

    caller_uid.high = CALLER_UID_HIGH;
    caller_uid.low  = CALLER_UID_LOW;

    memset(&caller_record, 0, sizeof(caller_record));
    caller_record.uid.high = RECORD_UID_HIGH;
    caller_record.uid.low  = RECORD_UID_LOW;

    status = 0x7E7E7E7E;
}

/* ============================================================================
 * The code under test
 * ============================================================================ */

#include "../get_attributes.c"
#include "../get_attr_info.c"

/* ============================================================================
 * FILE_$GET_ATTRIBUTES
 * ============================================================================ */

TEST(get_attributes_seeds_the_descriptor_uid_at_offset_8)
{
    uint16_t req = 0x0001;      /* byte 1 = 0x01: caller holds the lock */
    int16_t  size = FILE_ATTR_FULL_SIZE;
    uint8_t  out[AST_ATTR_REC_SIZE];

    reset();
    FILE_$GET_ATTRIBUTES(&caller_uid, &req, &size, &caller_record, out, &status);

    ASSERT_EQ(1, mock_ast_calls);
    /* The callee's record carries the CALLER's UID at +0x08 ... */
    ASSERT_EQ(CALLER_UID_HIGH, mock_ast_seen_uid.high);
    ASSERT_EQ(CALLER_UID_LOW,  mock_ast_seen_uid.low);
    /* ... with bit 6 of the +0x1D flags byte cleared. */
    ASSERT_EQ(0, mock_ast_seen_flagbyte & FILE_OBJ_LOC_SCRATCH);
    ASSERT_EQ(FILE_GET_ATTR_FLAGS_LOCKED, mock_ast_seen_flags);
    ASSERT_EQ(status_$ok, status);
}

TEST(get_attributes_copies_all_32_record_bytes_back)
{
    uint16_t req = 0x0001;
    int16_t  size = FILE_ATTR_FULL_SIZE;
    uint8_t  out[AST_ATTR_REC_SIZE];

    reset();
    FILE_$GET_ATTRIBUTES(&caller_uid, &req, &size, &caller_record, out, &status);

    ASSERT_EQ(0, memcmp(&caller_record, &mock_ast_record_out,
                        sizeof(file_$obj_loc_t)));
    ASSERT_EQ(0, memcmp(out, mock_ast_attrs, AST_ATTR_REC_SIZE));
}

TEST(get_attributes_copies_out_even_when_the_status_is_bad)
{
    uint16_t req = 0x0001;
    int16_t  size = FILE_ATTR_FULL_SIZE;
    uint8_t  out[AST_ATTR_REC_SIZE];

    reset();
    mock_ast_status = 0x00030006;
    FILE_$GET_ATTRIBUTES(&caller_uid, &req, &size, &caller_record, out, &status);

    /* 0x00E5DA20-0x00E5DA3C run unconditionally. */
    ASSERT_EQ(0, memcmp(out, mock_ast_attrs, AST_ATTR_REC_SIZE));
    ASSERT_EQ(0x00030006, status);
}

TEST(get_attributes_probe_reads_the_callers_record_uid)
{
    uint16_t req = 0x0002;      /* byte 1 = 0x02: run the delete probe */
    int16_t  size = FILE_ATTR_FULL_SIZE;
    uint8_t  out[AST_ATTR_REC_SIZE];

    reset();
    mock_delete_result = -1;    /* object is locked */
    FILE_$GET_ATTRIBUTES(&caller_uid, &req, &size, &caller_record, out, &status);

    ASSERT_EQ(1, mock_delete_calls);
    ASSERT_EQ(RECORD_UID_HIGH, mock_delete_seen_uid.high);
    ASSERT_EQ(RECORD_UID_LOW,  mock_delete_seen_uid.low);
    ASSERT_EQ(FILE_GET_ATTR_FLAGS_LOCKED, mock_ast_seen_flags);
}

TEST(get_attributes_probe_success_selects_flags_0x21)
{
    uint16_t req = 0x0002;
    int16_t  size = FILE_ATTR_FULL_SIZE;
    uint8_t  out[AST_ATTR_REC_SIZE];

    reset();
    mock_delete_result = 0;
    FILE_$GET_ATTRIBUTES(&caller_uid, &req, &size, &caller_record, out, &status);

    ASSERT_EQ(FILE_GET_ATTR_FLAGS_NORMAL, mock_ast_seen_flags);
}

TEST(get_attributes_bit2_skips_the_probe)
{
    uint16_t req = 0x0004;
    int16_t  size = FILE_ATTR_FULL_SIZE;
    uint8_t  out[AST_ATTR_REC_SIZE];

    reset();
    FILE_$GET_ATTRIBUTES(&caller_uid, &req, &size, &caller_record, out, &status);

    ASSERT_EQ(0, mock_delete_calls);
    ASSERT_EQ(FILE_GET_ATTR_FLAGS_NORMAL, mock_ast_seen_flags);
}

TEST(get_attributes_probe_runs_before_the_size_check)
{
    uint16_t req = 0x0002;
    int16_t  size = 0x10;       /* wrong size */
    uint8_t  out[AST_ATTR_REC_SIZE];

    reset();
    FILE_$GET_ATTRIBUTES(&caller_uid, &req, &size, &caller_record, out, &status);

    /* 0x00E5D9B6 comes before 0x00E5D9F2. */
    ASSERT_EQ(1, mock_delete_calls);
    ASSERT_EQ(0, mock_ast_calls);
    ASSERT_EQ(file_$invalid_arg, status);
}

TEST(get_attributes_rejects_an_empty_request_word)
{
    uint16_t req = 0x0000;
    int16_t  size = FILE_ATTR_FULL_SIZE;
    uint8_t  out[AST_ATTR_REC_SIZE];

    reset();
    FILE_$GET_ATTRIBUTES(&caller_uid, &req, &size, &caller_record, out, &status);

    ASSERT_EQ(0, mock_delete_calls);
    ASSERT_EQ(0, mock_ast_calls);
    ASSERT_EQ(file_$invalid_arg, status);
}

/* ============================================================================
 * FILE_$GET_ATTR_INFO
 * ============================================================================ */

TEST(attr_info_seeds_the_descriptor_and_copies_the_record_back)
{
    uint16_t req = 0x0001;
    int16_t  size = FILE_ATTR_INFO_SIZE;
    uint8_t  out[FILE_ATTR_INFO_SIZE];

    reset();
    FILE_$GET_ATTR_INFO(&caller_uid, &req, &size, &caller_record, out, &status);

    ASSERT_EQ(1, mock_ast_calls);
    ASSERT_EQ(CALLER_UID_HIGH, mock_ast_seen_uid.high);
    ASSERT_EQ(CALLER_UID_LOW,  mock_ast_seen_uid.low);
    ASSERT_EQ(0, mock_ast_seen_flagbyte & FILE_OBJ_LOC_SCRATCH);
    ASSERT_EQ(0, memcmp(&caller_record, &mock_ast_record_out,
                        sizeof(file_$obj_loc_t)));
    ASSERT_EQ(status_$ok, status);
}

TEST(attr_info_stops_before_the_copy_back_on_a_bad_status)
{
    uint16_t req = 0x0001;
    int16_t  size = FILE_ATTR_INFO_SIZE;
    uint8_t  out[FILE_ATTR_INFO_SIZE];
    file_$obj_loc_t before;

    reset();
    before = caller_record;
    mock_ast_status = 0x00030006;
    memset(out, 0xEE, sizeof(out));
    FILE_$GET_ATTR_INFO(&caller_uid, &req, &size, &caller_record, out, &status);

    ASSERT_EQ(0x00030006, status);
    /* 0x00E5D88A branches straight to the epilogue. */
    ASSERT_EQ(0, memcmp(&caller_record, &before, sizeof(file_$obj_loc_t)));
    ASSERT_EQ(0xEE, out[0]);
}

TEST(attr_info_copies_the_record_back_before_the_size_check)
{
    uint16_t req = 0x0001;
    int16_t  size = 0x10;       /* wrong size */
    uint8_t  out[FILE_ATTR_INFO_SIZE];

    reset();
    memset(out, 0xEE, sizeof(out));
    FILE_$GET_ATTR_INFO(&caller_uid, &req, &size, &caller_record, out, &status);

    /* 0x00E5D88E runs, then 0x00E5D89C fails. */
    ASSERT_EQ(0, memcmp(&caller_record, &mock_ast_record_out,
                        sizeof(file_$obj_loc_t)));
    ASSERT_EQ(file_$invalid_arg, status);
    ASSERT_EQ(0xEE, out[0]);
}

TEST(attr_info_repacks_the_compact_record)
{
    uint16_t req = 0x0001;
    int16_t  size = FILE_ATTR_INFO_SIZE;
    uint8_t  out[FILE_ATTR_INFO_SIZE];

    reset();
    /* attrs[i] == i, so every recovered field is its own source offset.  The
     * checks are byte comparisons so they hold on either byte order. */
    memset(out, 0, sizeof(out));
    FILE_$GET_ATTR_INFO(&caller_uid, &req, &size, &caller_record, out, &status);

    ASSERT_EQ(status_$ok, status);

    /* 0x00E5D8B0: attrs+0x00 = 00 01 02 03, masked with 0x00FF1F06, then
     * bits 1 and 0 of byte 2 and bit 7 of byte 3 rewritten from attrs+0x65
     * (= 0x65 = 0110 0101: bit 7 clear, bit 5 set, bit 4 clear). */
    ASSERT_EQ(0x00, out[0x00]);
    ASSERT_EQ(0x01, out[0x01]);
    ASSERT_EQ(0x01, out[0x02]);     /* 0x02 & 0x1F = 0x02, -bit1, +bit0 */
    ASSERT_EQ(0x02, out[0x03]);     /* 0x03 & 0x06 = 0x02, bit 7 stays clear */

    /* Every remaining store, source offset against destination offset. */
    ASSERT_EQ(0, memcmp(&out[0x04], &mock_ast_attrs[0x04], 24));
    ASSERT_EQ(0, memcmp(&out[0x1C], &mock_ast_attrs[0x3C], 4));
    ASSERT_EQ(0, memcmp(&out[0x20], &mock_ast_attrs[0x40], 4));
    ASSERT_EQ(0, memcmp(&out[0x24], &mock_ast_attrs[0x1C], 4));
    ASSERT_EQ(0, memcmp(&out[0x28], &mock_ast_attrs[0x20], 2));
    ASSERT_EQ(0, memcmp(&out[0x2C], &mock_ast_attrs[0x24], 4));
    ASSERT_EQ(0, memcmp(&out[0x30], &mock_ast_attrs[0x28], 2));
    ASSERT_EQ(0, memcmp(&out[0x32], &mock_ast_attrs[0x76], 2));
    ASSERT_EQ(0, memcmp(&out[0x36], &mock_ast_attrs[0x34], 4));
    ASSERT_EQ(0, memcmp(&out[0x3A], &mock_ast_attrs[0x38], 2));
    ASSERT_EQ(0, memcmp(&out[0x3C], &mock_ast_attrs[0x74], 2));
    ASSERT_EQ(0, memcmp(&out[0x3E], &mock_ast_attrs[0x78], 16));
    ASSERT_EQ(0, memcmp(&out[0x4E], &mock_ast_attrs[0x48], 28));
    ASSERT_EQ(mock_ast_attrs[0x64], out[0x6A]);
    ASSERT_EQ(0, memcmp(&out[0x6E], &mock_ast_attrs[0x68], 12));

    /* The three ranges the routine never writes stay at their reset value. */
    ASSERT_EQ(0, out[0x2A]);
    ASSERT_EQ(0, out[0x2B]);
    ASSERT_EQ(0, out[0x34]);
    ASSERT_EQ(0, out[0x35]);
    ASSERT_EQ(0, out[0x6B]);
    ASSERT_EQ(0, out[0x6C]);
    ASSERT_EQ(0, out[0x6D]);
}

TEST(attr_info_folds_the_access_byte_into_bytes_2_and_3)
{
    uint16_t req = 0x0001;
    int16_t  size = FILE_ATTR_INFO_SIZE;
    uint8_t  out[FILE_ATTR_INFO_SIZE];

    /* attrs+0x00 all ones, so the mask alone leaves 00 FF 1F 06. */
    reset();
    memset(mock_ast_attrs, 0, sizeof(mock_ast_attrs));
    memset(mock_ast_attrs, 0xFF, 4);
    mock_ast_attrs[0x65] = 0xB0;    /* bits 7, 5 and 4 all set */
    memset(out, 0, sizeof(out));
    FILE_$GET_ATTR_INFO(&caller_uid, &req, &size, &caller_record, out, &status);

    ASSERT_EQ(0x00, out[0x00]);
    ASSERT_EQ(0xFF, out[0x01]);
    ASSERT_EQ(0x1F, out[0x02]);     /* 0x1F with bits 1 and 0 set again */
    ASSERT_EQ(0x86, out[0x03]);     /* 0x06 with bit 7 set */

    reset();
    memset(mock_ast_attrs, 0, sizeof(mock_ast_attrs));
    memset(mock_ast_attrs, 0xFF, 4);
    mock_ast_attrs[0x65] = 0x00;    /* none of the three bits */
    memset(out, 0, sizeof(out));
    FILE_$GET_ATTR_INFO(&caller_uid, &req, &size, &caller_record, out, &status);

    ASSERT_EQ(0x1C, out[0x02]);     /* 0x1F with bits 1 and 0 cleared */
    ASSERT_EQ(0x06, out[0x03]);     /* 0x06 with bit 7 cleared */
}

TEST(attr_info_probe_reads_the_callers_record_uid)
{
    uint16_t req = 0x0002;
    int16_t  size = FILE_ATTR_INFO_SIZE;
    uint8_t  out[FILE_ATTR_INFO_SIZE];

    reset();
    mock_delete_result = -1;
    FILE_$GET_ATTR_INFO(&caller_uid, &req, &size, &caller_record, out, &status);

    ASSERT_EQ(1, mock_delete_calls);
    ASSERT_EQ(RECORD_UID_HIGH, mock_delete_seen_uid.high);
    ASSERT_EQ(RECORD_UID_LOW,  mock_delete_seen_uid.low);
    ASSERT_EQ(FILE_ATTR_INFO_FLAGS_LOCKED, mock_ast_seen_flags);
}

TEST(attr_info_rejects_an_empty_request_word)
{
    uint16_t req = 0x0000;
    int16_t  size = FILE_ATTR_INFO_SIZE;
    uint8_t  out[FILE_ATTR_INFO_SIZE];

    reset();
    FILE_$GET_ATTR_INFO(&caller_uid, &req, &size, &caller_record, out, &status);

    ASSERT_EQ(0, mock_ast_calls);
    ASSERT_EQ(file_$invalid_arg, status);
}

int main(void)
{
    printf("FILE_$GET_ATTRIBUTES / FILE_$GET_ATTR_INFO tests\n");

    RUN_TEST(get_attributes_seeds_the_descriptor_uid_at_offset_8);
    RUN_TEST(get_attributes_copies_all_32_record_bytes_back);
    RUN_TEST(get_attributes_copies_out_even_when_the_status_is_bad);
    RUN_TEST(get_attributes_probe_reads_the_callers_record_uid);
    RUN_TEST(get_attributes_probe_success_selects_flags_0x21);
    RUN_TEST(get_attributes_bit2_skips_the_probe);
    RUN_TEST(get_attributes_probe_runs_before_the_size_check);
    RUN_TEST(get_attributes_rejects_an_empty_request_word);

    RUN_TEST(attr_info_seeds_the_descriptor_and_copies_the_record_back);
    RUN_TEST(attr_info_stops_before_the_copy_back_on_a_bad_status);
    RUN_TEST(attr_info_copies_the_record_back_before_the_size_check);
    RUN_TEST(attr_info_repacks_the_compact_record);
    RUN_TEST(attr_info_folds_the_access_byte_into_bytes_2_and_3);
    RUN_TEST(attr_info_probe_reads_the_callers_record_uid);
    RUN_TEST(attr_info_rejects_an_empty_request_word);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
