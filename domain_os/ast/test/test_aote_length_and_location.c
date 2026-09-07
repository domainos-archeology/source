/*
 * Tests for the two AOTE words settled by beads source-traa and source-sy5u.
 *
 * Neither ast_$force_activate_segment (0x00E020FA) nor the length readers can
 * be driven on the host -- they reach absolute kernel addresses -- so this
 * test pins the ARITHMETIC instead, transcribed instruction for instruction
 * from the disassembly.  If either recovered meaning is ever revised, the
 * expressions below stop matching the addresses they cite.
 *
 *   aote+0x20 is the object's LENGTH IN BYTES over 1KB pages:
 *     0x00E0325E  AST_$TOUCH      (len - 1) >> 10  = index of the last page
 *     0x00E066C6  AST_$INVALIDATE same
 *     0x00E02AA6  ast_$setup_page_read: byte offset = page_index * 0x400
 *                 built as `lsl.l #0x8` then `lsl.l #0x2`
 *     0x00E02AB0  extend: len := byte_offset + 0x400
 *
 *   aote+0x08 / ast_$force_activate_segment's second argument is the object's
 *   LOCATION word:
 *     0x00E021B2  tst.w on the HIGH word -> the sign of the whole longword
 *     0x00E021D8  local:  low byte of (loc & 0x7FFFFFFF) -> aote+0xB8
 *     0x00E021E6  remote: loc & 0xFFFFF                  -> aote+0xB0
 *     0x00E02222  (loc & 0x7FFFFFFF) == 0 -> "location unknown"
 *
 *   bead source-xntu added the 0xA4..0xB7 arithmetic from the same routine:
 *     0x00E021A4  clr.b (0x9c,A3)          -> obj_uid.high &= 0x00FFFFFF
 *     0x00E021B8  andi.b #0x7f,(0xb9,A3)   -> remote_flag keeps bits 0..6
 *     0x00E021C6  bclr.b #0x6,(0xb9,A3)    -> and always loses bit 6
 *     0x00E0216E  bset #7 / three bclr     -> flags bits 0..3 survive
 *     0x00E0228C  vol_index <= 0x0F then the A5+0x420 bit test
 */

#include <stdio.h>
#include <stdint.h>

static int tests_failed;

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        unsigned long a_ = (unsigned long)(actual);                           \
        unsigned long e_ = (unsigned long)(expected);                         \
        if (a_ != e_) {                                                       \
            printf("  FAIL %s:%d: %s = 0x%lx, expected 0x%lx\n",              \
                   __FILE__, __LINE__, #actual, a_, e_);                      \
            tests_failed++;                                                   \
        }                                                                     \
    } while (0)

/* 0x00E0325E / 0x00E066C6: `subq.l #0x1,D1` / `lsr.l #0x8` / `lsr.l #0x2`. */
static uint32_t last_page_index(uint32_t length)
{
    return (length - 1) >> 10;
}

/* 0x00E02A96-0x00E02AA8: (seg * 32 + start + count - 1) then * 0x400. */
static uint32_t last_page_byte_offset(uint32_t segment, uint32_t start_page,
                                      uint32_t count)
{
    return (segment * 32u + start_page + count - 1u) << 10;
}

/* 0x00E021B2: the m68k `tst.w (0xc,A6)` reads the HIGH word of the longword. */
static int location_is_remote(uint32_t location)
{
    return (int32_t)location < 0;
}

/*
 * 0x00E021B2-0x00E021C6: `tst.w (0xc,A6)` / `smi D0b` /
 * `andi.b #0x7f,(0xb9,A3)` / `andi.b #-0x80,D0b` / `or.b D0b,(0xb9,A3)` /
 * `bclr.b #0x6,(0xb9,A3)` -- aote_t.remote_flag (obj_loc.flags).
 */
static uint8_t remote_flag_update(uint8_t old, uint32_t location)
{
    uint8_t v = (uint8_t)((old & 0x7F) |
                          (((int32_t)location < 0) ? 0x80 : 0x00));
    return (uint8_t)(v & (uint8_t)~0x40);
}

/*
 * 0x00E0216E-0x00E02180: `bset.b #7` then `bclr.b #6`, `#5` and `#4` on
 * aote_t.flags -- bits 0..3 of the recycled entry are carried over.
 */
static uint8_t init_flags(uint8_t old)
{
    uint8_t v = (uint8_t)(old | 0x80);
    v = (uint8_t)(v & (uint8_t)~0x40);
    v = (uint8_t)(v & (uint8_t)~0x20);
    return (uint8_t)(v & (uint8_t)~0x10);
}

/*
 * 0x00E0228C-0x00E0229A (and the identical tests at 0x00E022BE and
 * 0x00E02318): `cmp.w D0w,D1w` with D1 = 15 skips the mask when the volume
 * index is above 15; otherwise a set bit in the A5+0x420 word is a bad volume.
 */
static int volume_is_bad(uint8_t vol_index, uint16_t vol_flags)
{
    if (vol_index <= 0x0F) {
        return (vol_flags & (1 << vol_index)) != 0;
    }
    return 0;
}

static void test_length_is_bytes_over_1k_pages(void)
{
    printf("test_length_is_bytes_over_1k_pages\n");

    /* A one-byte object still owns page 0. */
    CHECK_EQ(last_page_index(1), 0);
    /* Exactly one full page. */
    CHECK_EQ(last_page_index(0x400), 0);
    /* One byte into the second page. */
    CHECK_EQ(last_page_index(0x401), 1);
    /* A full 32KB segment is pages 0..31. */
    CHECK_EQ(last_page_index(0x8000), 31);
    /* First page of segment 1. */
    CHECK_EQ(last_page_index(0x8001), 32);
}

static void test_setup_page_read_extension(void)
{
    printf("test_setup_page_read_extension\n");

    /*
     * 0x00E02AAA-0x00E02AB6: writing page 0 of segment 0 leaves the object
     * exactly one page long; the stored length is the byte offset of the last
     * page plus 0x400, i.e. rounded up to the next whole page.
     */
    uint32_t end = last_page_byte_offset(0, 0, 1);
    CHECK_EQ(end, 0);
    CHECK_EQ(end + 0x400, 0x400);
    CHECK_EQ(last_page_index(end + 0x400), 0);

    /* Writing pages 0..31 of segment 0 gives a 32KB object. */
    end = last_page_byte_offset(0, 0, 32);
    CHECK_EQ(end, 0x7C00);
    CHECK_EQ(end + 0x400, 0x8000);
    CHECK_EQ(last_page_index(end + 0x400), 31);

    /* Page 5 of segment 2 = absolute page 69. */
    end = last_page_byte_offset(2, 5, 1);
    CHECK_EQ(end, 69u * 0x400);
    CHECK_EQ(last_page_index(end + 0x400), 69);
}

static void test_location_word_decode(void)
{
    printf("test_location_word_decode\n");

    /* 0x00E02222: zero means "unknown", which is not the same as local vol 0. */
    CHECK_EQ((0x00000000u & 0x7FFFFFFFu) == 0, 1);
    CHECK_EQ(location_is_remote(0x00000000u), 0);

    /* Local: bit 31 clear, low byte is the logical volume index. */
    CHECK_EQ(location_is_remote(0x00000003u), 0);
    CHECK_EQ((uint8_t)(0x00000003u & 0x7FFFFFFFu), 3);
    CHECK_EQ((0x00000003u & 0x7FFFFFFFu) == 0, 0);

    /*
     * Remote: bit 31 set, bits 0..19 the node id, bits 20..30 the network.
     * 0x80312345 -> node 0x12345, network 0x003.
     */
    CHECK_EQ(location_is_remote(0x80312345u), 1);
    CHECK_EQ(0x80312345u & 0xFFFFFu, 0x12345u);
    CHECK_EQ((0x80312345u & 0xFFF00000u) | 0x12345u, 0x80312345u);

    /*
     * The sign test must look at the whole longword.  A word-wide test of the
     * LOW half -- the bug this pass removed from ast/force_activate_segment.c
     * -- would call this local; the m68k `tst.w (0xc,A6)` does not.
     */
    CHECK_EQ(location_is_remote(0x8000FFFFu), 1);
    CHECK_EQ((int16_t)(uint16_t)0xFFFFu < 0, 1); /* what the old cast tested */

    /* And a positive location whose low half is negative stays local. */
    CHECK_EQ(location_is_remote(0x0001FFFFu), 0);
}

static void test_obj_loc_region_stores(void)
{
    printf("test_obj_loc_region_stores\n");

    /*
     * 0x00E021A4: `clr.b (0x9c,A3)` clears only the first byte of the
     * embedded file_$obj_loc_t, i.e. the top byte of the longword at 0x9C.
     */
    CHECK_EQ(0xAABBCCDDu & 0x00FFFFFFu, 0x00BBCCDDu);
    CHECK_EQ(0x00BBCCDDu & 0x00FFFFFFu, 0x00BBCCDDu);

    /* 0x00E021E6-0x00E021F0: obj_loc_node := location & 0xFFFFF. */
    CHECK_EQ(0x80312345u & 0xFFFFFu, 0x12345u);
    CHECK_EQ(0xFFFFFFFFu & 0xFFFFFu, 0xFFFFFu);

    /* 0x00E021D2-0x00E021DC: local vol_index := low byte of loc & 0x7FFFFFFF. */
    CHECK_EQ((uint8_t)(0x0000000Fu & 0x7FFFFFFFu), 0x0F);
    CHECK_EQ((uint8_t)(0x12345678u & 0x7FFFFFFFu), 0x78);

    /* remote_flag: bit 7 tracks the location's sign, bit 6 always clears,
     * bits 0..5 are preserved. */
    CHECK_EQ(remote_flag_update(0x3F, 0x80000000u), 0xBF);
    CHECK_EQ(remote_flag_update(0x3F, 0x00000001u), 0x3F);
    CHECK_EQ(remote_flag_update(0xFF, 0x00000001u), 0x3F);
    CHECK_EQ(remote_flag_update(0x40, 0x80000000u), 0x80);
    /* the whole-longword sign test again, this time through the byte store */
    CHECK_EQ(remote_flag_update(0x00, 0x0001FFFFu), 0x00);

    /* flags: bits 0..3 of the recycled AOTE survive the four bit ops. */
    CHECK_EQ(init_flags(0x00), 0x80);
    CHECK_EQ(init_flags(0x7F), 0x8F);
    CHECK_EQ(init_flags(0xFF), 0x8F);

    /* volume checks: index above 15 skips the mask entirely. */
    CHECK_EQ(volume_is_bad(0x00, 0x0001), 1);
    CHECK_EQ(volume_is_bad(0x00, 0xFFFE), 0);
    CHECK_EQ(volume_is_bad(0x0F, 0x8000), 1);
    CHECK_EQ(volume_is_bad(0x10, 0xFFFF), 0);
    CHECK_EQ(volume_is_bad(0xFF, 0xFFFF), 0);
}

int main(void)
{
    printf("Running AOTE length/location tests...\n");

    test_length_is_bytes_over_1k_pages();
    test_setup_page_read_extension();
    test_location_word_decode();
    test_obj_loc_region_stores();

    if (tests_failed != 0) {
        printf("%d checks failed\n", tests_failed);
        return 1;
    }
    printf("All tests PASSED!\n");
    return 0;
}
