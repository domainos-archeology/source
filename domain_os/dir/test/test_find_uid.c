/*
 * dir/test/test_find_uid.c - the two dir_$find_uid_internal wrappers
 *
 * DIR_$FIND_UID (0x00E4E87C) and DIR_$FIND_NET (0x00E4E8B4) are both thin
 * argument shufflers, and both were transcribed wrong:
 *
 *   source-c34p  DIR_$FIND_UID's SIXTH argument is the caller's fifth
 *                parameter (`move.l (0x18,A6),-(SP)` at 0x00E4E890); the C
 *                passed the name-length pointer a second time, so the
 *                length the search wrote never reached the caller.
 *   source-5hyd  DIR_$FIND_NET builds its search UID out of an
 *                UNINITIALISED frame cell - only the low longword is
 *                touched, with `andi.l #-0x100000` then `or.l` of the WHOLE
 *                index - and its name buffer is the two bytes at A6-0x18,
 *                not 256.
 *
 * These tests capture what each wrapper hands dir_$find_uid_internal.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
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

#define ASSERT_PTR_EQ(expected, actual) do {                             \
    const void *_e = (const void *)(expected);                           \
    const void *_a = (const void *)(actual);                             \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Pointer mismatch at line %d\n", __LINE__);   \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "dir/dir_internal.h"

/* ------------------------------------------------------------------ */
/* The one callee both wrappers forward to                              */
/* ------------------------------------------------------------------ */

static int       fui_calls;
static uid_t    *fui_dir_uid;
static uid_t    *fui_target_uid;
static uid_t     fui_target_value;
static int8_t    fui_flag;
static int16_t   fui_name_buf_len;
static char     *fui_name_buf;
static int16_t  *fui_name_len_ret;
static uint32_t *fui_net_ret;
static status_$t *fui_status_ret;

/* What the mock writes back through its out-parameters. */
static int16_t   fui_out_name_len;
static uint32_t  fui_out_net;
static status_$t fui_out_status;

void dir_$find_uid_internal(uid_t *dir_uid, uid_t *target_uid, int8_t flag,
                            int16_t name_buf_len, char *name_buf,
                            int16_t *name_len_ret, uint32_t *net_ret,
                            status_$t *status_ret)
{
    fui_calls++;
    fui_dir_uid      = dir_uid;
    fui_target_uid   = target_uid;
    fui_target_value = *target_uid;
    fui_flag         = flag;
    fui_name_buf_len = name_buf_len;
    fui_name_buf     = name_buf;
    fui_name_len_ret = name_len_ret;
    fui_net_ret      = net_ret;
    fui_status_ret   = status_ret;

    *name_len_ret = fui_out_name_len;
    *net_ret      = fui_out_net;
    *status_ret   = fui_out_status;
}

#include "../find_uid.c"
#include "../find_net.c"

static void reset_mocks(void)
{
    fui_calls = 0;
    fui_dir_uid = NULL;
    fui_target_uid = NULL;
    memset(&fui_target_value, 0, sizeof(fui_target_value));
    fui_flag = 0x5A;
    fui_name_buf_len = -1;
    fui_name_buf = NULL;
    fui_name_len_ret = NULL;
    fui_net_ret = NULL;
    fui_status_ret = NULL;
    fui_out_name_len = 0;
    fui_out_net = 0;
    fui_out_status = status_$ok;
}

/* ------------------------------------------------------------------ */

/*
 * 0x00E4E888-0x00E4E8A4, right to left:
 *   (0x1c,A6) status_ret     arg 8
 *   -0x4(A6)  a LOCAL cell   arg 7   (the network address, discarded)
 *   (0x18,A6) name_len_ret   arg 6   <- the caller's FIFTH parameter
 *   (0x14,A6) name_buf       arg 5
 *   *(0x10,A6) the size WORD arg 4   (loaded through A0, by value)
 *   clr.w     false          arg 3
 *   (0xc,A6)  target_uid     arg 2
 *   (0x8,A6)  dir_uid        arg 1
 */
TEST(find_uid_forwards_its_five_pointers_in_the_image_order)
{
    uid_t     dir_uid    = { 0x11112222u, 0x33334444u };
    uid_t     target_uid = { 0x55556666u, 0x77778888u };
    uint16_t  name_buf_len = 0x0020;
    char      name_buf[0x20];
    int16_t   name_len_ret = -1;
    status_$t status = 0;

    reset_mocks();
    fui_out_name_len = 7;
    fui_out_status = 0x0BADF00D;

    DIR_$FIND_UID(&dir_uid, &target_uid, &name_buf_len, name_buf,
                  &name_len_ret, &status);

    ASSERT_EQ(1, fui_calls);
    ASSERT_PTR_EQ(&dir_uid, fui_dir_uid);
    ASSERT_PTR_EQ(&target_uid, fui_target_uid);
    /* arg 3 is `clr.w -(SP)`: a false Domain boolean, the UID search. */
    ASSERT_EQ(0, fui_flag);
    /* arg 4 is the size by VALUE, not the pointer. */
    ASSERT_EQ(0x0020, fui_name_buf_len);
    ASSERT_PTR_EQ(name_buf, fui_name_buf);
    /* source-c34p: arg 6 is the caller's fifth parameter, NOT a second
     * copy of &name_buf_len. */
    ASSERT_PTR_EQ(&name_len_ret, fui_name_len_ret);
    ASSERT_EQ(1, (int)(fui_name_len_ret != (int16_t *)&name_buf_len));
    ASSERT_PTR_EQ(&status, fui_status_ret);

    /* The length the search reports really does reach the caller now. */
    ASSERT_EQ(7, name_len_ret);
    ASSERT_EQ(0x0BADF00D, status);
    /* And the caller's buffer-size word is left alone. */
    ASSERT_EQ(0x0020, name_buf_len);
}

/*
 * arg 7 is `pea (-0x4,A6)`, the whole of this routine's 0x4-byte frame.  It
 * is not one of the caller's parameters and is never read back, so the
 * network address the search writes is discarded on this path.
 */
TEST(find_uid_gives_the_network_result_a_local_cell)
{
    uid_t     dir_uid    = { 1, 2 };
    uid_t     target_uid = { 3, 4 };
    uint16_t  name_buf_len = 4;
    char      name_buf[4];
    int16_t   name_len_ret = 0;
    status_$t status = 0;

    reset_mocks();
    fui_out_net = 0xDEADBEEFu;

    DIR_$FIND_UID(&dir_uid, &target_uid, &name_buf_len, name_buf,
                  &name_len_ret, &status);

    ASSERT_EQ(1, fui_calls);
    /* It is a real, writable cell... */
    ASSERT_EQ(1, (int)(fui_net_ret != NULL));
    /* ...but it aliases none of the caller's objects. */
    ASSERT_EQ(1, (int)((void *)fui_net_ret != (void *)&dir_uid));
    ASSERT_EQ(1, (int)((void *)fui_net_ret != (void *)&target_uid));
    ASSERT_EQ(1, (int)((void *)fui_net_ret != (void *)&name_buf_len));
    ASSERT_EQ(1, (int)((void *)fui_net_ret != (void *)name_buf));
    ASSERT_EQ(1, (int)((void *)fui_net_ret != (void *)&name_len_ret));
    ASSERT_EQ(1, (int)((void *)fui_net_ret != (void *)&status));
}

/*
 * 0x00E4E8D2-0x00E4E8EA: DIR_$FIND_NET's search UID is arg 2 and the flag
 * is `st -(SP)`, a TRUE Domain boolean.  arg 4 is `clr.w -(SP)`, so the
 * buffer length is 0 - which is why the two-byte name slot at A6-0x18 is
 * enough (source-5hyd).
 */
TEST(find_net_passes_a_true_flag_and_a_zero_length)
{
    uid_t    dir_uid = { 0xAAAA0001u, 0xAAAA0002u };
    uint32_t index = 0x000ABCDEu;
    uint32_t got;

    reset_mocks();
    fui_out_net = 0x12345678u;
    fui_out_status = status_$ok;

    got = DIR_$FIND_NET(&dir_uid, &index);

    ASSERT_EQ(1, fui_calls);
    ASSERT_PTR_EQ(&dir_uid, fui_dir_uid);
    /* `st -(SP)` is 0xFF, which reads back negative. */
    ASSERT_EQ(1, (int)(fui_flag < 0));
    ASSERT_EQ(0, fui_name_buf_len);
    /* The search UID is a LOCAL, never the caller's dir_uid. */
    ASSERT_EQ(1, (int)(fui_target_uid != &dir_uid));
    /* 0x00E4E8F6: the net result is returned when the status is zero. */
    ASSERT_EQ(0x12345678u, got);
}

/*
 * 0x00E4E8C0-0x00E4E8CE.  The low longword keeps its top 12 bits and takes
 * the WHOLE index longword - the index is NOT masked to 20 bits - and the
 * high longword is never written at all.  The only claim a test can make
 * about a deliberately uninitialised cell is about the bits the routine
 * does set, so check the index's own bits survive.
 */
TEST(find_net_ors_the_whole_index_into_the_low_longword)
{
    uid_t    dir_uid = { 0, 0 };
    uint32_t index;
    uint32_t low;

    /* An index whose bits reach above the 20-bit node field. */
    reset_mocks();
    index = 0x00FABCDEu;
    (void)DIR_$FIND_NET(&dir_uid, &index);
    low = fui_target_value.low;
    /* Every bit of the index is present, including the ones a 0xFFFFF mask
     * would have dropped. */
    ASSERT_EQ(index, low & index);
    ASSERT_EQ(0x00FABCDEu, low & 0x00FFFFFFu);

    /* And the caller's own UID is not copied in: the routine reads only its
     * own frame cell, so dir_uid.low cannot appear in the low half. */
    reset_mocks();
    index = 0;
    (void)DIR_$FIND_NET(&dir_uid, &index);
    /* The top 12 bits are whatever the frame held; the low 20 are the
     * index, i.e. zero. */
    ASSERT_EQ(0, fui_target_value.low & 0x000FFFFFu);
}

/* 0x00E4E8FA-0x00E4E900: a non-zero status clears the returned address. */
TEST(find_net_returns_zero_on_a_failed_search)
{
    uid_t    dir_uid = { 0, 0 };
    uint32_t index = 1;
    uint32_t got;

    reset_mocks();
    fui_out_net = 0xFFFFFFFFu;
    fui_out_status = status_$naming_name_not_found;

    got = DIR_$FIND_NET(&dir_uid, &index);

    ASSERT_EQ(1, fui_calls);
    ASSERT_EQ(0, got);
}

int main(void)
{
    printf("DIR_$FIND_UID / DIR_$FIND_NET tests\n");
    RUN_TEST(find_uid_forwards_its_five_pointers_in_the_image_order);
    RUN_TEST(find_uid_gives_the_network_result_a_local_cell);
    RUN_TEST(find_net_passes_a_true_flag_and_a_zero_length);
    RUN_TEST(find_net_ors_the_whole_index_into_the_low_longword);
    RUN_TEST(find_net_returns_zero_on_a_failed_search);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
