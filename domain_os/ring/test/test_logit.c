/*
 * ring/test/test_logit.c
 *
 * Layout and behaviour tests for RINGLOG_$LOGIT (0x00E1A20C) and the ring log
 * record it fills in.
 *
 * The entry base is the buffer base itself (0x00E1A2E4:
 * "movea.l #0xea3e38,A0 / moveq #0x2e,D1 / muls.w D3w,D1 /
 *  lea (0x0,A0,D1*0x1),A2"), so entry 0 shares its first word with the
 * next-entry index.  The packed node fields at 0x04..0x0B are written with
 * masked longword read-modify-writes that preserve the bits they do not own.
 */

#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ============================================================================
 * Stubs for the externals RINGLOG_$LOGIT calls
 * ============================================================================ */

#include "ring/ring_internal.h"
#include "ring/ringlog_internal.h"

ml_$spin_token_t ML_$SPIN_LOCK(void *lock)
{
    (void)lock;
    return 0;
}

void ML_$SPIN_UNLOCK(void *lock, ml_$spin_token_t token)
{
    (void)lock;
    (void)token;
}

/* The two module objects, normally defined by ring/ringlog_data.c */
MODULE_DATA_DEFINE(ringlog_ctl_t, RINGLOG_$CTL, 0x00E2C32C);
MODULE_DATA_DEFINE(ringlog_$data_t, RINGLOG_$DATA, 0x00EA3E38);

#include "../logit.c"

/* ============================================================================
 * Helpers
 * ============================================================================ */

static uint8_t pkt[0x60];
static uint8_t hdr_info[4];

static void reset_all(void)
{
    memset(&RINGLOG_$CTL, 0, sizeof(RINGLOG_$CTL));
    RINGLOG_$CTL.mbx_sock_filter = -1;
    RINGLOG_$CTL.who_sock_filter = -1;
    RINGLOG_$CTL.nil_sock_filter = -1;
    RINGLOG_$CTL.first_entry_flag = 0;
    memset(&RINGLOG_$DATA, 0, sizeof(RINGLOG_$DATA));
    memset(pkt, 0, sizeof(pkt));
    memset(hdr_info, 0, sizeof(hdr_info));

    /* a "receive" record: kind != 1, a socket type that passes the filters */
    pkt[0x0C] = 0;
    *(uint16_t *)&pkt[0x44] = 3;
    pkt[0x18] = 0;      /* word_off = 0x1E >> 1 = 0x0F, source byte 0x1E */
}

/* ============================================================================
 * Layout
 * ============================================================================ */

TEST(buffer_is_the_map_size) {
    /* SAU2 map: "D53 EA3E38 RINGLOG_$DATA ... size = 11FC" */
    ASSERT_EQ(0x11FCu, sizeof(ringlog_$data_t));
    ASSERT_EQ(0x11FCu, RINGLOG_DATA_SIZE);
}

TEST(entries_start_at_offset_zero) {
    /* entry n is buffer + 0x2E * n, so entry 0 IS the index word's home */
    ASSERT_EQ((uintptr_t)RINGLOG_$DATA.bytes,
              (uintptr_t)ringlog_$entry(0));
    ASSERT_EQ((uintptr_t)(RINGLOG_$DATA.bytes + 0x2E),
              (uintptr_t)ringlog_$entry(1));
    ASSERT_EQ((uintptr_t)(RINGLOG_$DATA.bytes + 99 * 0x2E),
              (uintptr_t)ringlog_$entry(99));
}

TEST(last_entry_fits_the_buffer) {
    /* entry 99 spans 0x11CA..0x11F9, inside the 0x11FC segment */
    ASSERT_EQ(1, (99 * RINGLOG_ENTRY_SIZE + (int)sizeof(ringlog_$entry_t))
                 <= (int)RINGLOG_DATA_SIZE);
}

TEST(entry_field_offsets) {
    ASSERT_EQ(0x00u, offsetof(ringlog_$entry_t, shared_head));
    ASSERT_EQ(0x02u, offsetof(ringlog_$entry_t, sock_byte_02));
    ASSERT_EQ(0x03u, offsetof(ringlog_$entry_t, sock_byte_03));
    ASSERT_EQ(0x04u, offsetof(ringlog_$entry_t, packed));
    ASSERT_EQ(0x0Cu, offsetof(ringlog_$entry_t, field_0c));
    ASSERT_EQ(0x10u, offsetof(ringlog_$entry_t, field_10));
    ASSERT_EQ(0x14u, offsetof(ringlog_$entry_t, pkt_type));
    ASSERT_EQ(0x16u, offsetof(ringlog_$entry_t, pkt_words));
}

TEST(packed_accessors_are_big_endian) {
    ringlog_$entry_t e;

    memset(&e, 0, sizeof(e));
    ringlog_$put_packed(&e, RINGLOG_PACKED_OFF_04, 0x11223344u);
    ASSERT_EQ(0x11u, e.packed[0]);
    ASSERT_EQ(0x22u, e.packed[1]);
    ASSERT_EQ(0x33u, e.packed[2]);
    ASSERT_EQ(0x44u, e.packed[3]);
    ASSERT_EQ(0x11223344u, ringlog_$get_packed(&e, RINGLOG_PACKED_OFF_04));

    /* the three views overlap: +0x06 starts two bytes into +0x04's longword */
    ASSERT_EQ(0x33440000u, ringlog_$get_packed(&e, RINGLOG_PACKED_OFF_06) & 0xFFFF0000u);
}

/* ============================================================================
 * The index word
 * ============================================================================ */

TEST(index_word_shares_entry_zero) {
    reset_all();
    ringlog_$set_index(0x1234);
    ASSERT_EQ(0x1234, ringlog_$get_index());
    /* it lives in entry 0's first two bytes */
    ASSERT_EQ(0x12u, RINGLOG_$DATA.bytes[0]);
    ASSERT_EQ(0x34u, RINGLOG_$DATA.bytes[1]);
}

TEST(index_wraps_after_99) {
    reset_all();
    ringlog_$set_index(99);
    ASSERT_EQ(99, RINGLOG_$LOGIT(hdr_info, pkt));
    ASSERT_EQ(0, ringlog_$get_index());
}

TEST(first_entry_flag_resets_the_index) {
    reset_all();
    ringlog_$set_index(40);
    RINGLOG_$CTL.first_entry_flag = -1;
    ASSERT_EQ(0, RINGLOG_$LOGIT(hdr_info, pkt));
    ASSERT_EQ(0, RINGLOG_$CTL.first_entry_flag);
}

/* ============================================================================
 * Filters
 * ============================================================================ */

TEST(id_filter_rejects_other_networks) {
    reset_all();
    RINGLOG_$CTL.filter_id = 0x11111111u;
    *(uint32_t *)&pkt[0x00] = 0x22222222u;
    *(uint32_t *)&pkt[0x08] = 0x33333333u;
    ASSERT_EQ(-1, RINGLOG_$LOGIT(hdr_info, pkt));

    /* a match at either +0x00 or +0x08 is enough */
    *(uint32_t *)&pkt[0x08] = 0x11111111u;
    ASSERT_EQ(0, RINGLOG_$LOGIT(hdr_info, pkt));
}

TEST(socket_filters_reject_when_enabled) {
    reset_all();
    RINGLOG_$CTL.who_sock_filter = 0;       /* >= 0 means "filter this out" */
    *(uint16_t *)&pkt[0x44] = (uint16_t)RINGLOG_SOCK_WHO;
    ASSERT_EQ(-1, RINGLOG_$LOGIT(hdr_info, pkt));

    RINGLOG_$CTL.who_sock_filter = -1;      /* < 0 disables the filter */
    ASSERT_EQ(0, RINGLOG_$LOGIT(hdr_info, pkt));
}

TEST(socket_type_above_0x0b_is_looked_up_again) {
    reset_all();
    RINGLOG_$CTL.nil_sock_filter = 0;
    *(uint16_t *)&pkt[0x44] = 0x0C;                 /* > 0x0B */
    *(uint16_t *)&pkt[0x38] = (uint16_t)0xFFFF;     /* the NIL socket */
    ASSERT_EQ(-1, RINGLOG_$LOGIT(hdr_info, pkt));
}

/* ============================================================================
 * The flag nibble
 * ============================================================================ */

TEST(flags_and_node_c_share_byte_0x0b) {
    ringlog_$entry_t *e;

    /*
     * The flag writes (0x00E1A2FA-0x00E1A31C) touch only bits 3..0 of the byte
     * at entry + 0x0B, and the node write that follows
     * ("andi.l #-0xfffff1,(0x8,A2)", 0x00E1A320) keeps exactly those four bits
     * while filling bits 7..4 with the node id's low nibble.
     */
    reset_all();
    e = ringlog_$entry(0);

    hdr_info[0] = 0x80;                 /* bit 7 -> INBOUND */
    pkt[0x0C] = 1;                      /* kind 1 -> SEND */
    *(uint16_t *)&pkt[0x1A] = 3;        /* the send-path socket type */
    *(uint32_t *)&pkt[0x00] = 0x000ABCDEu;

    ASSERT_EQ(0, RINGLOG_$LOGIT(hdr_info, pkt));

    /* low nibble = INBOUND|VALID|SEND, high nibble = 0xABCDE & 0xF */
    ASSERT_EQ(0xEEu, e->packed[RINGLOG_PACKED_OFF_08 + 3]);
}

TEST(inbound_flag_follows_header_bit7) {
    ringlog_$entry_t *e;

    reset_all();
    e = ringlog_$entry(0);
    e->packed[RINGLOG_PACKED_OFF_08 + 3] = 0x0F;    /* every flag set */
    hdr_info[0] = 0x00;
    ASSERT_EQ(0, RINGLOG_$LOGIT(hdr_info, pkt));
    /* INBOUND cleared, VALID set, SEND cleared (kind != 1), bit 0 preserved */
    ASSERT_EQ(0x05u, e->packed[RINGLOG_PACKED_OFF_08 + 3]);
}

/* ============================================================================
 * The packed node fields.  The three masked longword writes at 0x04, 0x06 and
 * 0x08 OVERLAP, and they run in that reverse order (0x08 first, then 0x06,
 * then 0x04), so each partly overwrites the one before.  The tests below pin
 * the whole eight-byte result rather than any one view.
 * ============================================================================ */

static void assert_packed_bytes(const ringlog_$entry_t *e, const uint8_t *want)
{
    int i;

    for (i = 0; i < 8; i++) {
        if (e->packed[i] != want[i]) {
            printf("FAILED\n    packed[%d]: expected 0x%02x, got 0x%02x\n",
                   i, want[i], e->packed[i]);
            tests_failed++;
            return;
        }
    }
}

TEST(receive_path_masks_the_source_nodes) {
    ringlog_$entry_t *e;
    /*
     *   flags byte 0x0B  : 0 -> VALID only                     = 0x04
     *   long@0x08 &= 0xFF00000F | (pkt[0x00] << 4)             pkt[0] = 0
     *   long@0x06 &= 0xF00000FF | ((pkt[0x34] & 0xFFFFFF) << 8)
     *   long@0x04 &= 0x00000FFF | ((pkt[0x40] & 0xFFFFFF) << 12)
     */
    static const uint8_t want[8] = { 0xAB, 0xCD, 0xE2, 0x34, 0x56, 0x00, 0x00, 0x04 };

    reset_all();
    e = ringlog_$entry(0);
    ringlog_$put_packed(e, RINGLOG_PACKED_OFF_06, 0xFFFFFFFFu);
    ringlog_$put_packed(e, RINGLOG_PACKED_OFF_04, 0xFFFFFFFFu);

    /* both sources are masked to 24 bits before the shift */
    *(uint32_t *)&pkt[0x34] = 0xAA123456u;
    *(uint32_t *)&pkt[0x40] = 0xBB0ABCDEu;

    ASSERT_EQ(0, RINGLOG_$LOGIT(hdr_info, pkt));
    assert_packed_bytes(e, want);
}

TEST(send_path_does_not_mask_the_sources) {
    ringlog_$entry_t *e;
    /*
     * The send arm (0x00E1A3CA / 0x00E1A3DA) applies NO 0xFFFFFF mask before
     * shifting, so a source above 24 bits shifts its high bits straight into
     * the neighbouring field.
     */
    static const uint8_t want[8] = { 0xAB, 0xCD, 0xE2, 0x34, 0x56, 0x23, 0x45, 0x66 };

    reset_all();
    e = ringlog_$entry(0);
    pkt[0x0C] = 1;
    *(uint16_t *)&pkt[0x1A] = 3;
    ringlog_$put_packed(e, RINGLOG_PACKED_OFF_06, 0);
    ringlog_$put_packed(e, RINGLOG_PACKED_OFF_04, 0);

    *(uint32_t *)&pkt[0x00] = 0x00123456u;
    *(uint32_t *)&pkt[0x08] = 0x000ABCDEu;

    ASSERT_EQ(0, RINGLOG_$LOGIT(hdr_info, pkt));
    assert_packed_bytes(e, want);
}

TEST(receive_path_copies_the_two_longwords) {
    ringlog_$entry_t *e;

    reset_all();
    e = ringlog_$entry(0);
    *(uint32_t *)&pkt[0x2E] = 0xCAFEBABEu;
    *(uint32_t *)&pkt[0x3A] = 0xDEADBEEFu;
    pkt[0x39] = 0x77;
    pkt[0x45] = 0x88;

    ASSERT_EQ(0, RINGLOG_$LOGIT(hdr_info, pkt));
    ASSERT_EQ(0xCAFEBABEu, e->field_10);
    ASSERT_EQ(0xDEADBEEFu, e->field_0c);
    ASSERT_EQ(0x77u, e->sock_byte_03);
    ASSERT_EQ(0x88u, e->sock_byte_02);
}

TEST(send_path_clears_the_two_longwords) {
    ringlog_$entry_t *e;

    reset_all();
    e = ringlog_$entry(0);
    e->field_0c = 0x11111111u;
    e->field_10 = 0x22222222u;
    pkt[0x0C] = 1;
    *(uint16_t *)&pkt[0x1A] = 3;
    pkt[0x1B] = 0x55;
    pkt[0x19] = 2;                  /* pkt[0x1F + 4] */
    pkt[0x1F + 4] = 0x66;

    ASSERT_EQ(0, RINGLOG_$LOGIT(hdr_info, pkt));
    ASSERT_EQ(0u, e->field_0c);
    ASSERT_EQ(0u, e->field_10);
    ASSERT_EQ(0x55u, e->sock_byte_02);
    ASSERT_EQ(0x66u, e->sock_byte_03);
}

TEST(packet_words_come_from_the_computed_offset) {
    ringlog_$entry_t *e;
    int i;

    reset_all();
    e = ringlog_$entry(0);
    pkt[0x18] = 4;                  /* (4 + 0x1E) >> 1 = 0x11, byte 0x22 */
    for (i = 0; i < 13; i++) {
        *(uint16_t *)&pkt[0x22 + 2 * i] = (uint16_t)(0x1000 + i);
    }
    *(uint16_t *)&pkt[0x16] = 0x9ABC;

    ASSERT_EQ(0, RINGLOG_$LOGIT(hdr_info, pkt));
    ASSERT_EQ(0x9ABCu, e->pkt_type);
    for (i = 0; i < 13; i++) {
        ASSERT_EQ((uint16_t)(0x1000 + i), e->pkt_words[i]);
    }
}

TEST(thirteenth_word_lands_in_the_next_entry) {
    ringlog_$entry_t *e0;

    reset_all();
    e0 = ringlog_$entry(0);
    pkt[0x18] = 0;                  /* source starts at pkt + 0x1E */
    *(uint16_t *)&pkt[0x1E + 24] = 0x5A5A;      /* the 13th word */

    ASSERT_EQ(0, RINGLOG_$LOGIT(hdr_info, pkt));
    /* entry 0 + 0x2E is entry 1's shared word */
    ASSERT_EQ(0x5A5Au, e0->pkt_words[12]);
    ASSERT_EQ((uintptr_t)&e0->pkt_words[12],
              (uintptr_t)&RINGLOG_$DATA.bytes[RINGLOG_ENTRY_SIZE]);
}

int main(void)
{
    printf("RINGLOG_$LOGIT tests\n");
    RUN_TEST(buffer_is_the_map_size);
    RUN_TEST(entries_start_at_offset_zero);
    RUN_TEST(last_entry_fits_the_buffer);
    RUN_TEST(entry_field_offsets);
    RUN_TEST(packed_accessors_are_big_endian);
    RUN_TEST(index_word_shares_entry_zero);
    RUN_TEST(index_wraps_after_99);
    RUN_TEST(first_entry_flag_resets_the_index);
    RUN_TEST(id_filter_rejects_other_networks);
    RUN_TEST(socket_filters_reject_when_enabled);
    RUN_TEST(socket_type_above_0x0b_is_looked_up_again);
    RUN_TEST(flags_and_node_c_share_byte_0x0b);
    RUN_TEST(inbound_flag_follows_header_bit7);
    RUN_TEST(receive_path_masks_the_source_nodes);
    RUN_TEST(send_path_does_not_mask_the_sources);
    RUN_TEST(receive_path_copies_the_two_longwords);
    RUN_TEST(send_path_clears_the_two_longwords);
    RUN_TEST(packet_words_come_from_the_computed_offset);
    RUN_TEST(thirteenth_word_lands_in_the_next_entry);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
