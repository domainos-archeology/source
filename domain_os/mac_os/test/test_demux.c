/*
 * mac_os/test/test_demux.c
 *
 * MAC_OS_$DEMUX (0x00E0B816) looks the received frame type up in the port's
 * packet-type table, resolves the channel, stamps the arrival time and the
 * CHANNEL ENTRY'S ADDRESS into the driver record, and calls the channel's
 * receive entry.
 *
 * Bead source-d6vh: the body used to sit under "#if defined(ARCH_M68K)" with
 * a not-implemented stub, so none of it was reachable on the host.  These
 * tests exercise the ported body: the two shared error exits (0x00E0B85E and
 * 0x00E0B882 both land on 0x00E0B888), the 0xF4 port stride, the 12-byte
 * entry stride with the channel index at +0x08, the 0x14 channel stride, the
 * time split at 0x00E0B890 / 0x00E0B896 and the callback argument list at
 * 0x00E0B8A4-0x00E0B8B2.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %-48s ", #name);          \
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

#define ASSERT_TRUE(cond) do {                                           \
    if (!(cond)) {                                                       \
        printf("FAILED\n    %s at line %d\n", #cond, __LINE__);          \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "mac_os/mac_os_internal.h"

/* ------------------------------------------------------------------ */
/* Module data and stubs                                               */

mac_os_$channel_t        MAC_OS_$CHANNEL_TABLE[MAC_OS_CHANNEL_TABLE_SLOTS];
mac_os_$port_pkt_table_t MAC_OS_$PORT_PKT_TABLES[MAC_OS_MAX_PORTS];

/* The clock TIME_$ABS_CLOCK hands back (0x00E0B832). */
static clock_t mock_clock;
static int     clock_calls;

void TIME_$ABS_CLOCK(clock_t *out)
{
    clock_calls++;
    *out = mock_clock;
}

/*
 * 0x00E0B84A-0x00E0B85A: the arguments the image pushes, and the answer the
 * test wants back.
 */
static uint32_t  find_frame_type;
static void     *find_entries;
static int16_t   find_count;
static int       find_calls;
static int16_t   find_result;

int16_t MAC_OS_$FIND_PACKET_TYPE(uint32_t frame_type,
                                 mac_os_$pkt_type_entry_t *entries,
                                 int16_t count)
{
    find_calls++;
    find_frame_type = frame_type;
    find_entries    = entries;
    find_count      = count;
    return find_result;
}

/* The channel's receive entry. */
static mac_os_$rcv_pkt_t *cb_pkt;
static int16_t           *cb_port_num;
static void              *cb_param3;
static status_$t         *cb_status;
static int                cb_calls;
static status_$t          cb_sets_status;

static void mock_callback(mac_os_$rcv_pkt_t *pkt_info, int16_t *port_num,
                          void *param3, status_$t *status_ret)
{
    cb_calls++;
    cb_pkt      = pkt_info;
    cb_port_num = port_num;
    cb_param3   = param3;
    cb_status   = status_ret;
    *status_ret = cb_sets_status;
}

#include "../demux.c"

/* ------------------------------------------------------------------ */
/* Fixture                                                             */

static mac_os_$rcv_pkt_t pkt;
static int16_t           port_num;
static status_$t         status;
static int               param3_object;

static void reset_all(void)
{
    memset(MAC_OS_$CHANNEL_TABLE, 0, sizeof(MAC_OS_$CHANNEL_TABLE));
    memset(MAC_OS_$PORT_PKT_TABLES, 0, sizeof(MAC_OS_$PORT_PKT_TABLES));
    memset(&pkt, 0, sizeof(pkt));

    /*
     * mac_os_$channel_t.callback and mac_os_$rcv_pkt_t.channel are 32-bit
     * target VAs, so aim the host VA arena just below the lowest address the
     * test needs to encode - a 64-bit host pointer does not survive the
     * uint32_t on its own.  The margin keeps every encoded VA non-zero, since
     * VA zero is NIL on the target.
     */
    {
        uintptr_t lo = (uintptr_t)&MAC_OS_$CHANNEL_TABLE[0];
        if ((uintptr_t)(void *)mock_callback < lo) {
            lo = (uintptr_t)(void *)mock_callback;
        }
        ARCH_HOST_VA_BASE = lo - 0x1000u;
    }

    mock_clock.high = 0x11223344u;
    mock_clock.low  = 0x5566u;
    clock_calls     = 0;

    find_calls  = 0;
    find_result = 0;

    cb_calls       = 0;
    cb_sets_status = status_$ok;
    cb_pkt         = NULL;
    cb_port_num    = NULL;
    cb_param3      = NULL;
    cb_status      = NULL;

    port_num = 1;
    status   = 0x5A5A5A5A;

    /* Port 1's table: one entry naming channel 3. */
    MAC_OS_$PORT_PKT_TABLES[1].entry_count = 1;
    MAC_OS_$PORT_PKT_TABLES[1].entries[0].channel_index = 3;

    MAC_OS_$CHANNEL_TABLE[3].callback = ARCH_PTR_TO_VA((void *)mock_callback);

    pkt.frame_type = 0x600;
}

/* ------------------------------------------------------------------ */
/* Layout                                                              */

/* 0x00E0B842 mulu.w #0xf4 / 0x00E0B874 the 0x14 channel stride. */
TEST(table_strides_match_the_image)
{
    ASSERT_EQ(0xF4u, sizeof(mac_os_$port_pkt_table_t));
    ASSERT_EQ(0x14u, sizeof(mac_os_$channel_t));
    ASSERT_EQ(0x0Cu, sizeof(mac_os_$pkt_type_entry_t));
    ASSERT_EQ(0x04u, offsetof(mac_os_$port_pkt_table_t, entries));
    ASSERT_EQ(0x08u, offsetof(mac_os_$pkt_type_entry_t, channel_index));
    ASSERT_EQ(0x00u, offsetof(mac_os_$channel_t, callback));
}

/* ------------------------------------------------------------------ */
/* Behaviour                                                           */

/*
 * 0x00E0B830, 0x00E0B832, 0x00E0B84A: the status cell is cleared first, the
 * clock is read next, and the lookup gets the frame type, the entry array and
 * the count from the port's own table.
 */
TEST(lookup_gets_the_ports_table)
{
    reset_all();

    MAC_OS_$DEMUX(&pkt, &port_num, &param3_object, &status);

    ASSERT_EQ(1, clock_calls);
    ASSERT_EQ(1, find_calls);
    ASSERT_EQ(0x600u, find_frame_type);
    ASSERT_TRUE(find_entries == &MAC_OS_$PORT_PKT_TABLES[1].entries[0]);
    ASSERT_EQ(1u, find_count);
}

/* 0x00E0B85E cmpi.w #-0x1 / beq 0x00E0B888. */
TEST(a_lookup_miss_is_the_shared_error)
{
    reset_all();
    find_result = -1;

    MAC_OS_$DEMUX(&pkt, &port_num, &param3_object, &status);

    ASSERT_EQ(status_$mac_XXX_unknown, status);
    ASSERT_EQ(0, cb_calls);
}

/* 0x00E0B882 tst.l (0x7a0,A2) / beq 0x00E0B888 - the same error code. */
TEST(a_channel_with_no_callback_is_the_shared_error)
{
    reset_all();
    MAC_OS_$CHANNEL_TABLE[3].callback = 0;

    MAC_OS_$DEMUX(&pkt, &port_num, &param3_object, &status);

    ASSERT_EQ(status_$mac_XXX_unknown, status);
    ASSERT_EQ(0, cb_calls);
    /* Nothing was stamped into the record. */
    ASSERT_EQ(0u, pkt.time_high);
    ASSERT_EQ(0u, pkt.channel);
}

/*
 * 0x00E0B864-0x00E0B880: the channel index is the word at entry + 0x08 of the
 * entry the lookup named, and the channel entry is that index times 0x14.
 */
TEST(the_channel_index_comes_from_entry_plus_8)
{
    reset_all();
    MAC_OS_$PORT_PKT_TABLES[1].entry_count = 3;
    MAC_OS_$PORT_PKT_TABLES[1].entries[2].channel_index = 5;
    MAC_OS_$CHANNEL_TABLE[3].callback = 0;
    MAC_OS_$CHANNEL_TABLE[5].callback = ARCH_PTR_TO_VA((void *)mock_callback);
    find_result = 2;

    MAC_OS_$DEMUX(&pkt, &port_num, &param3_object, &status);

    ASSERT_EQ(1, cb_calls);
    ASSERT_EQ(ARCH_PTR_TO_VA(&MAC_OS_$CHANNEL_TABLE[5]), pkt.channel);
}

/*
 * 0x00E0B890 move.l (-0x4c,A6),(0x2a,A4) and 0x00E0B896
 * move.w (-0x48,A6),(0x2e,A4): a longword then a WORD.
 */
TEST(the_arrival_time_is_a_longword_then_a_word)
{
    reset_all();

    MAC_OS_$DEMUX(&pkt, &port_num, &param3_object, &status);

    ASSERT_EQ(0x11223344u, pkt.time_high);
    ASSERT_EQ(0x5566u,     pkt.time_low);
}

/*
 * 0x00E0B89C lea (0x7a0,A2),A0 / move.l A0,(0x34,A4): the channel ENTRY's
 * address, not its index.
 */
TEST(the_record_gets_the_channel_entry_address)
{
    reset_all();

    MAC_OS_$DEMUX(&pkt, &port_num, &param3_object, &status);

    ASSERT_EQ(ARCH_PTR_TO_VA(&MAC_OS_$CHANNEL_TABLE[3]), pkt.channel);
    ASSERT_TRUE(pkt.channel != 3);
}

/*
 * 0x00E0B8A4-0x00E0B8B2: the callback is handed the descriptor, the caller's
 * port-number POINTER, the caller's third argument and the caller's status
 * cell - and whatever it stores there is what the caller sees.
 */
TEST(the_callback_gets_the_callers_own_cells)
{
    reset_all();
    cb_sets_status = 0x00123456;

    MAC_OS_$DEMUX(&pkt, &port_num, &param3_object, &status);

    ASSERT_EQ(1, cb_calls);
    ASSERT_TRUE(cb_pkt == &pkt);
    ASSERT_TRUE(cb_port_num == &port_num);
    ASSERT_TRUE(cb_param3 == &param3_object);
    ASSERT_TRUE(cb_status == &status);
    ASSERT_EQ(0x00123456, status);
}

/* 0x00E0B83E-0x00E0B846: the port index selects the table. */
TEST(the_port_number_selects_the_table)
{
    reset_all();
    port_num = 4;
    MAC_OS_$PORT_PKT_TABLES[4].entry_count = 9;
    MAC_OS_$PORT_PKT_TABLES[4].entries[0].channel_index = 3;

    MAC_OS_$DEMUX(&pkt, &port_num, &param3_object, &status);

    ASSERT_TRUE(find_entries == &MAC_OS_$PORT_PKT_TABLES[4].entries[0]);
    ASSERT_EQ(9u, find_count);
}

int main(void)
{
    printf("MAC_OS_$DEMUX tests\n");
    RUN_TEST(table_strides_match_the_image);
    RUN_TEST(lookup_gets_the_ports_table);
    RUN_TEST(a_lookup_miss_is_the_shared_error);
    RUN_TEST(a_channel_with_no_callback_is_the_shared_error);
    RUN_TEST(the_channel_index_comes_from_entry_plus_8);
    RUN_TEST(the_arrival_time_is_a_longword_then_a_word);
    RUN_TEST(the_record_gets_the_channel_entry_address);
    RUN_TEST(the_callback_gets_the_callers_own_cells);
    RUN_TEST(the_port_number_selects_the_table);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
