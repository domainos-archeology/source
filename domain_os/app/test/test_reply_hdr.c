/*
 * app/test/test_reply_hdr.c - the shared APP reply header prefix
 *
 * Bead source-ca0z.  APP_$RECEIVE builds an eight-byte prefix at the head of
 * the record app_$receive_rec_t.reply points at (0x00E00980-0x00E009AC):
 *
 *   0x00E00984  move.w #0x118,(A0)          magic
 *   0x00E009AC  move.w (0x12,A1),(0x2,A0)   template_len
 *   0x00E009A6  move.w (-0x16,A6),(0x4,A0)  data_len
 *   0x00E009A0  move.w (0x16,A1),(0x6,A0)   request_id
 *
 * Four protocols read that prefix at those offsets and then continue into
 * their own tail.  These tests pin the prefix layout, pin that both long
 * records still put their protocol tails where their own disassembly reads
 * them, and pin that a prefix built once is readable through every view.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %-46s ", #name);          \
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

#include "app/app.h"
#include "msg/msg_internal.h"
#include "asknode/asknode_internal.h"

/* ------------------------------------------------------------------ */

/* 0x00E00984 / 0x00E009A0 / 0x00E009A6 / 0x00E009AC. */
TEST(prefix_is_eight_bytes_at_the_image_offsets)
{
    ASSERT_EQ(8u, sizeof(app_$reply_hdr_t));
    ASSERT_EQ(0x00u, offsetof(app_$reply_hdr_t, magic));
    ASSERT_EQ(0x02u, offsetof(app_$reply_hdr_t, template_len));
    ASSERT_EQ(0x04u, offsetof(app_$reply_hdr_t, data_len));
    ASSERT_EQ(0x06u, offsetof(app_$reply_hdr_t, request_id));
}

/*
 * MSG_$$RCV_INTERNAL (0x00E59548) reads 0x02, 0x04 and 0x06 and then its own
 * tail at 0x08 (dest_node), 0x0C, 0x0E (an UNALIGNED longword), 0x12, 0x14,
 * 0x15 and 0x16.
 */
TEST(msg_record_keeps_its_tail_offsets)
{
    ASSERT_EQ(0x00u, offsetof(msg_$reply_hdr_t, prefix));
    ASSERT_EQ(0x02u, offsetof(msg_$reply_hdr_t, prefix.template_len));
    ASSERT_EQ(0x04u, offsetof(msg_$reply_hdr_t, prefix.data_len));
    ASSERT_EQ(0x06u, offsetof(msg_$reply_hdr_t, prefix.request_id));
    ASSERT_EQ(0x08u, offsetof(msg_$reply_hdr_t, dest_node));
    ASSERT_EQ(0x0Cu, offsetof(msg_$reply_hdr_t, dest_sock));
    ASSERT_EQ(0x0Eu, offsetof(msg_$reply_hdr_t, src_node));
    ASSERT_EQ(0x12u, offsetof(msg_$reply_hdr_t, src_sock));
    ASSERT_EQ(0x14u, offsetof(msg_$reply_hdr_t, proto_family));
    ASSERT_EQ(0x15u, offsetof(msg_$reply_hdr_t, proto_type));
    ASSERT_EQ(0x16u, offsetof(msg_$reply_hdr_t, proto_subtype));
}

/*
 * ASKNODE_$SERVER (0x00E659xx) and ASKNODE_$WHO_NOTOPO (0x00E66258) read the
 * prefix plus 0x08, 0x0E (UNALIGNED), 0x12 and 0x14.
 */
TEST(asknode_record_keeps_its_tail_offsets)
{
    ASSERT_EQ(0x00u, offsetof(asknode_$reply_hdr_t, prefix));
    ASSERT_EQ(0x02u, offsetof(asknode_$reply_hdr_t, prefix.template_len));
    ASSERT_EQ(0x04u, offsetof(asknode_$reply_hdr_t, prefix.data_len));
    ASSERT_EQ(0x06u, offsetof(asknode_$reply_hdr_t, prefix.request_id));
    ASSERT_EQ(0x08u, offsetof(asknode_$reply_hdr_t, sender_node));
    ASSERT_EQ(0x0Cu, offsetof(asknode_$reply_hdr_t, f0c));
    ASSERT_EQ(0x0Eu, offsetof(asknode_$reply_hdr_t, node_id));
    ASSERT_EQ(0x12u, offsetof(asknode_$reply_hdr_t, src_socket));
    ASSERT_EQ(0x14u, offsetof(asknode_$reply_hdr_t, f14));
    ASSERT_EQ(0x16u, sizeof(asknode_$reply_hdr_t));
}

/*
 * One buffer, four views.  The bytes are what APP_$RECEIVE would leave
 * behind: 0x0118, then a template length, a data length and a request id.
 */
TEST(one_prefix_reads_the_same_through_every_view)
{
    static const uint8_t raw[8] = {
        0x01, 0x18,     /* magic        */
        0x00, 0x18,     /* template_len */
        0x00, 0x40,     /* data_len     */
        0x12, 0x34      /* request_id   */
    };
    uint8_t buf[sizeof(msg_$reply_hdr_t)];
    const app_$reply_hdr_t     *bare;
    const msg_$reply_hdr_t     *as_msg;
    const asknode_$reply_hdr_t *as_asknode;

    memset(buf, 0, sizeof(buf));
    memcpy(buf, raw, sizeof(raw));

    bare       = (const app_$reply_hdr_t *)buf;
    as_msg     = (const msg_$reply_hdr_t *)buf;
    as_asknode = (const asknode_$reply_hdr_t *)buf;

    /* Read as big-endian words the way the m68k does, so the test says the
     * same thing on either host byte order. */
    ASSERT_EQ(0x0118u, (unsigned)((raw[0] << 8) | raw[1]));

    ASSERT_EQ(bare->magic,        as_msg->prefix.magic);
    ASSERT_EQ(bare->template_len, as_msg->prefix.template_len);
    ASSERT_EQ(bare->data_len,     as_msg->prefix.data_len);
    ASSERT_EQ(bare->request_id,   as_msg->prefix.request_id);

    ASSERT_EQ(bare->magic,        as_asknode->prefix.magic);
    ASSERT_EQ(bare->template_len, as_asknode->prefix.template_len);
    ASSERT_EQ(bare->data_len,     as_asknode->prefix.data_len);
    ASSERT_EQ(bare->request_id,   as_asknode->prefix.request_id);
}

/*
 * rem_file/ and rip/ use app_$reply_hdr_t on its own; REM_FILE reads
 * template_len (0x00E6126E), data_len (0x00E61262) and request_id
 * (0x00E61266), RIP_$INIT reads data_len (0x00E2FD1E) and request_id
 * (0x00E2FD28).  Both take the record straight off APP_$RECEIVE's reply VA,
 * which is only two-byte aligned, so the record must stay packed.
 */
TEST(bare_prefix_survives_an_odd_alignment)
{
    uint8_t backing[16];
    app_$reply_hdr_t *hdr;

    memset(backing, 0, sizeof(backing));
    hdr = (app_$reply_hdr_t *)(backing + 2);

    hdr->magic        = 0x0118;
    hdr->template_len = 0x0018;
    hdr->data_len     = 0x0040;
    hdr->request_id   = 0x1234;

    ASSERT_EQ(0x0118u, hdr->magic);
    ASSERT_EQ(0x0018u, hdr->template_len);
    ASSERT_EQ(0x0040u, hdr->data_len);
    ASSERT_EQ(0x1234,  hdr->request_id);

    /* Nothing spilled past the eight bytes. */
    ASSERT_EQ(0u, backing[0]);
    ASSERT_EQ(0u, backing[1]);
    ASSERT_EQ(0u, backing[10]);
}

int main(void)
{
    printf("app reply header prefix tests\n");
    RUN_TEST(prefix_is_eight_bytes_at_the_image_offsets);
    RUN_TEST(msg_record_keeps_its_tail_offsets);
    RUN_TEST(asknode_record_keeps_its_tail_offsets);
    RUN_TEST(one_prefix_reads_the_same_through_every_view);
    RUN_TEST(bare_prefix_survives_an_odd_alignment);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
