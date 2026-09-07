/*
 * pacct/test/test_log.c - Unit tests that drive PACCT_$LOG (0x00E5AA9C)
 * itself, through stubs for every routine it calls (bead source-a5co).
 *
 * The real pacct/log.c and pacct/compress.c are #included below;
 * pacct_$clock_to_comp (0x00E5AA28) is stubbed so that the store at record
 * offset 0x40 is distinguishable from every other comp_t store.
 *
 * PACCT_$LOG builds the record in its frame at A6-0x190 and then copies 32
 * longwords out of it (0x00E5ACEE `lea (-0x190,A6),A0` / `moveq #0x1f,D1` /
 * `move.l (A0)+,(A1)+`), so a record offset is (displacement + 0x190).  The
 * complete set of stores into that frame, in program order:
 *
 *   0x00E5AAFC  clr.w  (-0x190,A6)                 0x00  2   0
 *   0x00E5AB08  andi.b #-0x2,(-0x18f,A6)           0x01  1   clear bit 0
 *   0x00E5AB0E  or.b   D0b,(-0x18f,A6)             0x01  1   *fork_flag >> 7
 *   0x00E5AB1A  andi.b #-0x3,(-0x18f,A6)           0x01  1   clear bit 1
 *   0x00E5AB22  or.b   D0b,(-0x18f,A6)             0x01  1   (*su_flag >> 7) << 1
 *   0x00E5AB2A  move.b (0x1,A0),(-0x18e,A6)        0x02  1   low byte of *exit_status
 *   0x00E5AB3C  move.l (A1)+,(A0)+  x9, to -0x18c  0x04  36  sids   (SIDS arg 1)
 *   0x00E5AB4A  move.l (A0)+,(A1)+  x3, to -0x168  0x28  12  prot   (SIDS arg 3)
 *   0x00E5AB50  clr.l  (-0x15c,A6)                 0x34  4   0
 *   0x00E5AB54  clr.l  (-0x14e,A6)                 0x42  4   0
 *   0x00E5AB60  move.w D0w,(-0x14a,A6)             0x46  2   compress(*io_write)
 *   0x00E5AB6C  move.w D0w,(-0x148,A6)             0x48  2   compress(*io_read)
 *   0x00E5AB86  move.w D0w,(-0x11e,A6)             0x72  2   compress(60*(w+r))
 *   0x00E5AB96  move.w D0w,(-0x154,A6)             0x3C  2   compress(proc_times[2])
 *   0x00E5ABA6  move.w D0w,(-0x152,A6)             0x3E  2   compress(proc_times[3])
 *   0x00E5ABBA  move.l D0,(-0x158,A6)              0x38  4   CLOCK_TO_SEC + 0x12CEA600
 *   0x00E5ABD6  move.w D0w,(-0x150,A6)             0x40  2   clock_to_comp(elapsed)
 *   0x00E5ABDE  move.l (A0)+,(-0x146,A6)           0x4A  4   proc_uid.high
 *   0x00E5ABE2  move.l (A0)+,(-0x142,A6)           0x4E  4   proc_uid.low
 *   0x00E5AC04  move.b (-0x1,A2,D0w),(-0x13f,A1)   0x52+ 1   comm_ptr[i], i < min(len,32)
 *   0x00E5AC20  clr.b  (-0x13f,A1)                 0x52+ 1   zero fill through 0x71
 *   0x00E5AC5C  move.l D1,(-0x15c,A6)              0x34  4   -1, or the zero-extended
 *                                                              word at attr_info+0x32
 *
 * Offsets 0x03 and 0x74..0x7F are never stored; they are copied out of the
 * frame uninitialised.  That is the original behaviour and is not asserted.
 */

#include <stdio.h>
#include <string.h>

#include "pacct/pacct_internal.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed;

#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
    current_failed = 0;                         \
    name();                                     \
    if (current_failed) {                       \
        tests_failed++;                         \
        printf("FAIL\n");                       \
    } else {                                    \
        tests_passed++;                         \
        printf("ok\n");                         \
    }                                           \
} while (0)

#define ASSERT_EQ(actual, expected, what) do {                          \
    unsigned long _a = (unsigned long)(actual);                         \
    unsigned long _e = (unsigned long)(expected);                       \
    if (_a != _e) {                                                     \
        printf("\n    %s: got 0x%lx, expected 0x%lx", (what), _a, _e);  \
        current_failed = 1;                                             \
    }                                                                   \
} while (0)

/* ============================================================================
 * Globals the code under test references
 * ============================================================================ */

uid_t          UID_$NIL;
pacct_state_t  pacct_state;

/* ============================================================================
 * Stubs
 * ============================================================================ */

/* Values the stubs hand back, and what they recorded. */
static clock_t  stub_now;               /* what TIME_$CLOCK returns */
static clock_t  stub_sub48_dst_in;      /* SUB48's dst before the subtraction */
static clock_t *stub_sub48_dst;
static clock_t *stub_sub48_src;
static clock_t *stub_c2c_arg;           /* pacct_$clock_to_comp's argument */
static clock_t  stub_c2c_val;           /* and what it held */
static clock_t *stub_cts_arg;           /* CAL_$CLOCK_TO_SEC's argument */
static uint32_t stub_cts_ret;

static int       stub_enter_super;
static int       stub_exit_super;

static status_$t stub_attr_status;
static uint16_t  stub_attr_devno;
static uid_t    *stub_attr_uid;
static void     *stub_attr_req;
static int16_t  *stub_attr_size;

static int       stub_unmap_calls;
static int16_t   stub_unmap_mode;
static uid_t    *stub_unmap_uid;
static uint32_t  stub_unmap_start;
static uint32_t  stub_unmap_size;
static uint16_t  stub_unmap_asid;

static int       stub_maps_calls;
static int16_t   stub_maps_mode;
static int16_t   stub_maps_flags;
static uid_t    *stub_maps_uid;
static uint32_t  stub_maps_offset;
static uint32_t  stub_maps_length;
static int16_t   stub_maps_prot;
static uint32_t  stub_maps_hint;
static int8_t    stub_maps_create;
static status_$t stub_maps_status;      /* status the stub reports */
static uint32_t  stub_maps_out;         /* length it writes through `out` */
static void     *stub_maps_ret;

static int       stub_setlen_calls;
static uid_t    *stub_setlen_uid;
static uint32_t  stub_setlen_len;

/* The four blocks ACL_$GET_RE_ALL_SIDS writes; only 1 and 3 reach the record. */
static uint8_t stub_sids1[0x24];
static uint8_t stub_sids2[0x24];
static uint8_t stub_prot3[0x0C];
static uint8_t stub_prot4[0x0C];

void TIME_$CLOCK(clock_t *clock)
{
    *clock = stub_now;
}

void ACL_$GET_RE_ALL_SIDS(void *acl_data, void *re_sids,
                          void *prot_info, void *subsys_ids,
                          status_$t *status)
{
    memcpy(acl_data,   stub_sids1, sizeof(stub_sids1));
    memcpy(re_sids,    stub_sids2, sizeof(stub_sids2));
    memcpy(prot_info,  stub_prot3, sizeof(stub_prot3));
    memcpy(subsys_ids, stub_prot4, sizeof(stub_prot4));
    *status = status_$ok;
}

ulong CAL_$CLOCK_TO_SEC(clock_t *clock)
{
    stub_cts_arg = clock;
    return stub_cts_ret;
}

/* 48-bit dst -= src, exactly as 0x00E172E4 does it. */
int8_t SUB48(clock_t *dst, clock_t *src)
{
    uint64_t a, b, r;

    stub_sub48_dst = dst;
    stub_sub48_src = src;
    stub_sub48_dst_in = *dst;

    a = ((uint64_t)dst->high << 16) | dst->low;
    b = ((uint64_t)src->high << 16) | src->low;
    r = (a - b) & 0xFFFFFFFFFFFFull;
    dst->high = (uint32_t)(r >> 16);
    dst->low  = (uint16_t)(r & 0xFFFF);
    return 0;
}

comp_t pacct_$clock_to_comp(clock_t *clock)
{
    stub_c2c_arg = clock;
    stub_c2c_val = *clock;
    return 0x5A5A;
}

void FILE_$GET_ATTR_INFO(uid_t *file_uid, void *param_2, int16_t *size_ptr,
                         file_$obj_loc_t *loc_rec, void *attr_out,
                         status_$t *status_ret)
{
    stub_attr_uid  = file_uid;
    stub_attr_req  = param_2;
    stub_attr_size = size_ptr;
    memset(loc_rec, 0xEE, sizeof(*loc_rec));
    memset(attr_out, 0x11, FILE_ATTR_INFO_SIZE);
    /* 0x00E5AC58 reads a word at the compact record's +0x32. */
    memcpy((uint8_t *)attr_out + 0x32, &stub_attr_devno, sizeof(stub_attr_devno));
    *status_ret = stub_attr_status;
}

void ACL_$ENTER_SUPER(void) { stub_enter_super++; }
void ACL_$EXIT_SUPER(void)  { stub_exit_super++; }

void MST_$UNMAP_PRIVI(int16_t mode, uid_t *uid, uint32_t start, uint32_t size,
                      uint16_t asid, status_$t *status_ret)
{
    stub_unmap_calls++;
    stub_unmap_mode  = mode;
    stub_unmap_uid   = uid;
    stub_unmap_start = start;
    stub_unmap_size  = size;
    stub_unmap_asid  = asid;
    *status_ret = status_$ok;
}

void *MST_$MAPS(int16_t mode, int16_t flags, uid_t *uid, uint32_t offset,
                uint32_t length, int16_t prot, uint32_t hint, int8_t create,
                void *out, status_$t *status)
{
    stub_maps_calls++;
    stub_maps_mode   = mode;
    stub_maps_flags  = flags;
    stub_maps_uid    = uid;
    stub_maps_offset = offset;
    stub_maps_length = length;
    stub_maps_prot   = prot;
    stub_maps_hint   = hint;
    stub_maps_create = create;
    *(uint32_t *)out = stub_maps_out;
    *status = stub_maps_status;
    return stub_maps_ret;
}

void FILE_$SET_LEN(uid_t *file_uid, uint32_t *new_length, status_$t *status_ret)
{
    stub_setlen_calls++;
    stub_setlen_uid = file_uid;
    stub_setlen_len = *new_length;
    *status_ret = status_$ok;
}

/* ============================================================================
 * Code under test
 * ============================================================================ */

#include "../compress.c"
#include "../log.c"

/* ============================================================================
 * Fixture
 * ============================================================================ */

/* Where the 32-longword copy at 0x00E5ACEE lands. */
static uint32_t out_words[64];
static uint8_t *const out = (uint8_t *)out_words;

static boolean  in_fork;
static boolean  in_su;
static int16_t  in_exit;
static clock_t  in_start;
static uint32_t in_times[4];
static uint32_t in_write;
static uint32_t in_read;
static uid_t    in_tty_uid;
static uid_t    in_proc_uid;
static char     in_comm[64];
static int16_t  in_comm_len;

static void call_log(void)
{
    PACCT_$LOG(&in_fork, &in_su, &in_exit, &in_start, in_times,
               &in_write, &in_read, &in_tty_uid, &in_proc_uid,
               in_comm, &in_comm_len);
}

static uint16_t u16_at(size_t off)
{
    uint16_t v;
    memcpy(&v, out + off, sizeof(v));
    return v;
}

static uint32_t u32_at(size_t off)
{
    uint32_t v;
    memcpy(&v, out + off, sizeof(v));
    return v;
}

static void reset(void)
{
    unsigned i;

    memset(out_words, 0xA5, sizeof(out_words));

    UID_$NIL.high = 0;
    UID_$NIL.low  = 0;

    memset(&pacct_state, 0, sizeof(pacct_state));
    pacct_state.owner.high    = 0x11111111;
    pacct_state.owner.low     = 0x22222222;
    pacct_state.buf_remaining = 0x8000;
    pacct_state.write_ptr     = out_words;
    pacct_state.map_ptr       = out_words;
    pacct_state.map_offset    = 0x8000;
    pacct_state.file_pos      = 0x4000;

    for (i = 0; i < sizeof(stub_sids1); i++) { stub_sids1[i] = (uint8_t)(0xA0 + i); }
    for (i = 0; i < sizeof(stub_sids2); i++) { stub_sids2[i] = 0xBB; }
    for (i = 0; i < sizeof(stub_prot3); i++) { stub_prot3[i] = (uint8_t)(0xC0 + i); }
    for (i = 0; i < sizeof(stub_prot4); i++) { stub_prot4[i] = 0xDD; }

    stub_now.high = 0x00030000;
    stub_now.low  = 0x0000;
    stub_cts_ret  = 0x01000000;
    stub_cts_arg  = NULL;
    stub_c2c_arg  = NULL;
    stub_sub48_dst = NULL;
    stub_sub48_src = NULL;

    stub_enter_super = 0;
    stub_exit_super  = 0;

    stub_attr_status = status_$ok;
    stub_attr_devno  = 0x0BEE;

    stub_unmap_calls  = 0;
    stub_maps_calls   = 0;
    stub_maps_status  = status_$ok;
    stub_maps_out     = 0x8000;
    stub_maps_ret     = out_words;
    stub_setlen_calls = 0;

    in_fork = (boolean)0x80;
    in_su   = (boolean)0x00;
    in_exit = 0x1234;
    in_start.high = 0x00010000;
    in_start.low  = 0x0000;
    in_times[0] = 0xDEADBEEF;
    in_times[1] = 0xDEADBEEF;
    in_times[2] = 0x30;
    in_times[3] = 0x40;
    in_write = 100;
    in_read  = 7;
    in_tty_uid.high  = 0x33333333;
    in_tty_uid.low   = 0x44444444;
    in_proc_uid.high = 0x0BADF00D;
    in_proc_uid.low  = 0x0D15EA5E;
    memset(in_comm, 0x7E, sizeof(in_comm));
    memcpy(in_comm, "abcdefgh", 8);
    in_comm_len = 8;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

/* Every store above, at the offset its `_Static_assert` in pacct/pacct.h claims. */
static void test_every_record_store(void)
{
    unsigned i;

    reset();
    call_log();

    ASSERT_EQ(u16_at(0x00), 0x0001, "0x00 ac_flags (0x00E5AAFC/0x00E5AB0E)");
    ASSERT_EQ(out[0x02],    0x34,   "0x02 ac_stat (0x00E5AB2A, low byte of 0x1234)");

    for (i = 0; i < 0x24; i++) {
        ASSERT_EQ(out[0x04 + i], (uint8_t)(0xA0 + i), "0x04 ac_sids (0x00E5AB3C)");
    }
    for (i = 0; i < 0x0C; i++) {
        ASSERT_EQ(out[0x28 + i], (uint8_t)(0xC0 + i), "0x28 ac_prot (0x00E5AB4A)");
    }

    ASSERT_EQ(u32_at(0x34), 0x0BEE,     "0x34 ac_devno (0x00E5AC5C)");
    ASSERT_EQ(u32_at(0x38), 0x13CEA600, "0x38 ac_btime (0x00E5ABBA)");
    ASSERT_EQ(u16_at(0x3C), pacct_$compress(0x30), "0x3C ac_utime (0x00E5AB96)");
    ASSERT_EQ(u16_at(0x3E), pacct_$compress(0x40), "0x3E ac_stime (0x00E5ABA6)");
    ASSERT_EQ(u16_at(0x40), 0x5A5A,     "0x40 ac_etime (0x00E5ABD6)");
    ASSERT_EQ(u32_at(0x42), 0,          "0x42 ac_zero_42 (0x00E5AB54)");
    ASSERT_EQ(u16_at(0x46), pacct_$compress(100), "0x46 ac_io_write (0x00E5AB60)");
    ASSERT_EQ(u16_at(0x48), pacct_$compress(7),   "0x48 ac_io_read (0x00E5AB6C)");
    ASSERT_EQ(u32_at(0x4A), 0x0BADF00D, "0x4A ac_proc_uid.high (0x00E5ABDE)");
    ASSERT_EQ(u32_at(0x4E), 0x0D15EA5E, "0x4E ac_proc_uid.low (0x00E5ABE2)");

    for (i = 0; i < 8; i++) {
        ASSERT_EQ(out[0x52 + i], "abcdefgh"[i], "0x52 ac_comm (0x00E5AC04)");
    }
    for (i = 8; i < 0x20; i++) {
        ASSERT_EQ(out[0x52 + i], 0, "0x52 ac_comm zero fill (0x00E5AC20)");
    }

    /* 60 * (*io_write + *io_read), built at 0x00E5AB74-0x00E5AB7C. */
    ASSERT_EQ(u16_at(0x72), pacct_$compress((100u + 7u) * 60u),
              "0x72 ac_mem (0x00E5AB86)");
}

/* The record takes SIDS argument 1 and argument 3, never 2 or 4. */
static void test_sid_and_prot_come_from_args_one_and_three(void)
{
    unsigned i;

    reset();
    call_log();

    for (i = 0; i < 0x24; i++) {
        if (out[0x04 + i] == 0xBB) {
            ASSERT_EQ(out[0x04 + i], (uint8_t)(0xA0 + i),
                      "0x04 ac_sids took SIDS arg 2");
        }
    }
    for (i = 0; i < 0x0C; i++) {
        if (out[0x28 + i] == 0xDD) {
            ASSERT_EQ(out[0x28 + i], (uint8_t)(0xC0 + i),
                      "0x28 ac_prot took SIDS arg 4");
        }
    }
}

/*
 * 0x00E5AB06 / 0x00E5AB18 are LOGICAL shifts of bit 7, so a byte with other
 * bits set but bit 7 clear contributes nothing.
 */
static void test_flag_bits_are_bit_seven_only(void)
{
    reset(); in_fork = (boolean)0x00; in_su = (boolean)0x00; call_log();
    ASSERT_EQ(u16_at(0x00), 0x0000, "flags 00/00");

    reset(); in_fork = (boolean)0xFF; in_su = (boolean)0x00; call_log();
    ASSERT_EQ(u16_at(0x00), 0x0001, "flags FF/00 (bit 0, 0x00E5AB0E)");

    reset(); in_fork = (boolean)0x00; in_su = (boolean)0xFF; call_log();
    ASSERT_EQ(u16_at(0x00), 0x0002, "flags 00/FF (bit 1, 0x00E5AB22)");

    reset(); in_fork = (boolean)0x80; in_su = (boolean)0x80; call_log();
    ASSERT_EQ(u16_at(0x00), 0x0003, "flags 80/80");

    /* 0x7F has every bit but bit 7. */
    reset(); in_fork = (boolean)0x7F; in_su = (boolean)0x7F; call_log();
    ASSERT_EQ(u16_at(0x00), 0x0000, "flags 7F/7F must stay clear");
}

/* 0x00E5ABE6-0x00E5AC2A clips the name at 32 and never runs past 0x71. */
static void test_comm_is_clipped_at_thirty_two(void)
{
    unsigned i;

    reset();
    memset(in_comm, 'Z', sizeof(in_comm));
    in_comm_len = 40;
    call_log();

    for (i = 0; i < 0x20; i++) {
        ASSERT_EQ(out[0x52 + i], 'Z', "0x52 ac_comm clipped copy");
    }
    /* ac_mem still holds its own store, so the copy stopped at 0x71. */
    ASSERT_EQ(u16_at(0x72), pacct_$compress((100u + 7u) * 60u),
              "0x72 ac_mem survives a 40-byte name");

    reset();
    in_comm_len = 0;
    call_log();
    for (i = 0; i < 0x20; i++) {
        ASSERT_EQ(out[0x52 + i], 0, "0x52 ac_comm all zero for len 0");
    }
    ASSERT_EQ(u32_at(0x4E), 0x0D15EA5E, "0x4E untouched by a zero-length name");
}

/* 0x00E5AC4C-0x00E5AC5C: -1 on a failed lookup, else the word at +0x32. */
static void test_devno_paths(void)
{
    reset();
    stub_attr_status = 0x000B0006;
    call_log();
    ASSERT_EQ(u32_at(0x34), 0xFFFFFFFFu, "0x34 ac_devno = -1 (0x00E5AC52)");

    reset();
    stub_attr_devno = 0xFFFF;
    call_log();
    ASSERT_EQ(u32_at(0x34), 0x0000FFFFu, "0x34 ac_devno zero-extends (0x00E5AC58)");

    reset();
    call_log();
    ASSERT_EQ(stub_attr_uid, &in_tty_uid, "GET_ATTR_INFO arg 1 (0x00E5AC3E)");
    ASSERT_EQ(*(uint16_t *)stub_attr_req, 0x0004, "GET_ATTR_INFO arg 2 (0xE5AD34)");
    ASSERT_EQ(*stub_attr_size, FILE_ATTR_INFO_SIZE, "GET_ATTR_INFO arg 3 (0xE5A8B8)");
}

/*
 * 0x00E5ABBE-0x00E5ABD6: SUB48 runs in the current-clock slot, and
 * pacct_$clock_to_comp reads that same slot.
 */
static void test_elapsed_is_computed_in_the_current_clock_slot(void)
{
    reset();
    call_log();

    ASSERT_EQ(stub_cts_arg, &in_start, "CLOCK_TO_SEC took start_clock (0x00E5ABAA)");
    ASSERT_EQ(stub_sub48_src, &in_start, "SUB48 src is start_clock (0x00E5ABBE)");
    ASSERT_EQ(stub_sub48_dst, stub_c2c_arg, "SUB48 dst is clock_to_comp's arg");
    ASSERT_EQ(stub_sub48_dst_in.high, stub_now.high, "SUB48 dst held TIME_$CLOCK's value");
    /* 0x00030000_0000 - 0x00010000_0000 in 16.32 clock form. */
    ASSERT_EQ(stub_c2c_val.high, 0x00020000u, "elapsed high");
    ASSERT_EQ(stub_c2c_val.low, 0x0000, "elapsed low");
    ASSERT_EQ(u16_at(0x40), 0x5A5A, "0x40 ac_etime (0x00E5ABD6)");
}

/* 0x00E5AC60-0x00E5AD24: the buffer bookkeeping around the copy. */
static void test_buffer_bookkeeping_without_a_remap(void)
{
    reset();
    call_log();

    ASSERT_EQ(stub_enter_super, 1, "ACL_$ENTER_SUPER (0x00E5AC60)");
    ASSERT_EQ(stub_exit_super, 1, "ACL_$EXIT_SUPER (0x00E5AD24)");
    ASSERT_EQ(stub_maps_calls, 0, "no MST_$MAPS while 0x8000 bytes remain");
    ASSERT_EQ(stub_unmap_calls, 0, "no MST_$UNMAP_PRIVI");

    ASSERT_EQ(pacct_state.buf_remaining, 0x8000 - 0x80, "buf_remaining (0x00E5AD04)");
    ASSERT_EQ(pacct_state.write_ptr, out_words + 0x20, "write_ptr (0x00E5AD08)");
    ASSERT_EQ(pacct_state.file_pos, 0x4000 + 0x80, "file_pos (0x00E5AD0C)");

    ASSERT_EQ(stub_setlen_calls, 1, "FILE_$SET_LEN (0x00E5AD1A)");
    ASSERT_EQ(stub_setlen_uid, &pacct_state.owner, "SET_LEN arg 1 (0x00E5AD18)");
    ASSERT_EQ(stub_setlen_len, 0x4000 + 0x80, "SET_LEN arg 2 (0x00E5AD14)");
}

/*
 * 0x00E5AC66 `cmpi.l #0x80,(0xc,A5)` / `bge`: fewer than 0x80 bytes left means
 * unmap what is there and map the next 32KB region.
 */
static void test_remap_path(void)
{
    static uint32_t new_region[64];
    uint32_t old_va;

    reset();
    pacct_state.buf_remaining = 0x7F;
    pacct_state.map_offset    = 0x8000;
    pacct_state.map_ptr       = out_words;
    stub_maps_ret             = new_region;
    stub_maps_out             = 0x8000;
    old_va = ARCH_PTR_TO_VA(out_words);
    memset(new_region, 0xA5, sizeof(new_region));

    call_log();

    ASSERT_EQ(stub_unmap_calls, 1, "MST_$UNMAP_PRIVI (0x00E5AC8E)");
    ASSERT_EQ(stub_unmap_mode, 1, "UNMAP arg 1 (0x00E5AC8A)");
    ASSERT_EQ(stub_unmap_uid, &UID_$NIL, "UNMAP arg 2 (0x00E5AC84)");
    ASSERT_EQ(stub_unmap_start, old_va, "UNMAP arg 3 (0x00E5AC80, map_ptr by value)");
    ASSERT_EQ(stub_unmap_size, 0x8000, "UNMAP arg 4 (0x00E5AC7C, map_offset)");
    ASSERT_EQ(stub_unmap_asid, 0, "UNMAP arg 5 (0x00E5AC7A)");

    ASSERT_EQ(stub_maps_calls, 1, "MST_$MAPS (0x00E5ACC4)");
    ASSERT_EQ(stub_maps_mode, 0, "MAPS arg 1 (0x00E5ACC2)");
    /* 0x00E5ACC0 `st -(SP)` sets the BYTE at A6+0x0A, the high half of the
     * word slot, which MST_$MAPS reads at 0x00E43998. */
    ASSERT_EQ((uint16_t)stub_maps_flags, 0xFF00, "MAPS arg 2 byte (0x00E5ACC0)");
    ASSERT_EQ(stub_maps_uid, &pacct_state.owner, "MAPS arg 3 (0x00E5ACBE)");
    ASSERT_EQ(stub_maps_offset, 0x4000, "MAPS arg 4 (0x00E5ACBA, file_pos)");
    ASSERT_EQ(stub_maps_length, 0x8000, "MAPS arg 5 (0x00E5ACB4)");
    ASSERT_EQ(stub_maps_prot, 0x16, "MAPS arg 6 (0x00E5ACB0)");
    ASSERT_EQ(stub_maps_hint, 0, "MAPS arg 7 (0x00E5ACAE)");
    ASSERT_EQ((uint8_t)stub_maps_create, 0xFF, "MAPS arg 8 (0x00E5ACAC)");

    /* 0x00E5ACE2-0x00E5ACE8 then 0x00E5AD04-0x00E5AD0C. */
    ASSERT_EQ(pacct_state.map_ptr, new_region, "map_ptr (0x00E5ACCE)");
    ASSERT_EQ(pacct_state.buf_remaining, 0x8000 - 0x80, "buf_remaining after remap");
    ASSERT_EQ(pacct_state.write_ptr, new_region + 0x20, "write_ptr after remap");

    /* The record went to the new region, not the old one. */
    ASSERT_EQ(((uint8_t *)new_region)[0x02], 0x34, "record landed in the new region");
    ASSERT_EQ(out[0x02], 0xA5, "old region untouched");
}

/* 0x00E5ACD2-0x00E5ACE0: a failed map clears the pointers and skips the copy. */
static void test_remap_failure(void)
{
    reset();
    pacct_state.buf_remaining = 0x00;
    pacct_state.map_ptr       = NULL;
    stub_maps_status          = 0x00120005;

    call_log();

    ASSERT_EQ(stub_unmap_calls, 0, "no unmap when map_ptr is nil (0x00E5AC74)");
    ASSERT_EQ(stub_maps_calls, 1, "MST_$MAPS still called");
    ASSERT_EQ(pacct_state.map_ptr, NULL, "map_ptr cleared (0x00E5ACD8)");
    ASSERT_EQ(pacct_state.buf_remaining, 0, "buf_remaining cleared (0x00E5ACDC)");
    ASSERT_EQ(pacct_state.file_pos, 0x4000, "file_pos untouched");
    ASSERT_EQ(stub_setlen_calls, 0, "no FILE_$SET_LEN");
    ASSERT_EQ(stub_exit_super, 1, "ACL_$EXIT_SUPER still runs (0x00E5AD24)");
    ASSERT_EQ(out[0x02], 0xA5, "no record copied");
}

/* 0x00E5AABE-0x00E5AACE: owner == UID_$NIL means accounting is off. */
static void test_disabled_when_owner_is_nil(void)
{
    reset();
    pacct_state.owner.high = 0;
    pacct_state.owner.low  = 0;

    call_log();

    ASSERT_EQ(stub_enter_super, 0, "no ACL_$ENTER_SUPER when disabled");
    ASSERT_EQ(stub_exit_super, 0, "no ACL_$EXIT_SUPER when disabled");
    ASSERT_EQ(stub_setlen_calls, 0, "no FILE_$SET_LEN when disabled");
    ASSERT_EQ(pacct_state.file_pos, 0x4000, "file_pos untouched when disabled");
    ASSERT_EQ(out[0x02], 0xA5, "no record copied when disabled");

    /* Only the low longword differing is still "enabled" (0x00E5AACC). */
    reset();
    pacct_state.owner.high = 0;
    pacct_state.owner.low  = 1;
    call_log();
    ASSERT_EQ(stub_enter_super, 1, "high-only match is not UID_$NIL");
}

/* 0x00E5ACEE copies exactly 32 longwords - not 31, not 33. */
static void test_copy_is_exactly_thirty_two_longwords(void)
{
    reset();
    call_log();

    ASSERT_EQ(out_words[0x20], 0xA5A5A5A5u, "0x80 is past the record");
    ASSERT_EQ(out_words[0x21], 0xA5A5A5A5u, "0x84 is past the record");
}

int main(void)
{
    printf("PACCT_$LOG (0x00E5AA9C) tests\n");

    RUN_TEST(test_every_record_store);
    RUN_TEST(test_sid_and_prot_come_from_args_one_and_three);
    RUN_TEST(test_flag_bits_are_bit_seven_only);
    RUN_TEST(test_comm_is_clipped_at_thirty_two);
    RUN_TEST(test_devno_paths);
    RUN_TEST(test_elapsed_is_computed_in_the_current_clock_slot);
    RUN_TEST(test_buffer_bookkeeping_without_a_remap);
    RUN_TEST(test_remap_path);
    RUN_TEST(test_remap_failure);
    RUN_TEST(test_disabled_when_owner_is_nil);
    RUN_TEST(test_copy_is_exactly_thirty_two_longwords);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
