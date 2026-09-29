/*
 * mac_os/test/test_init.c
 *
 * MAC_OS_$INIT (0x00E2F4FC) clears the channel flags through the WORD's HIGH
 * byte:
 *   0x00E2F5FE  bclr.b #0x0,(0x7b2,A0)   -> word bit 8, PROMISCUOUS
 *   0x00E2F604  bclr.b #0x1,(0x7b2,A0)   -> word bit 9, IN_USE
 * so bits 0 and 1 of the word must survive.  It also seeds each configured
 * port's own link and XNS addresses from NODE_$ME.
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
 * Module data and stubs
 * ============================================================================ */

#include "mac_os/mac_os_internal.h"

mac_os_$channel_t        MAC_OS_$CHANNEL_TABLE[MAC_OS_CHANNEL_TABLE_SLOTS];
mac_os_$port_pkt_table_t MAC_OS_$PORT_PKT_TABLES[MAC_OS_MAX_PORTS];
mac_os_$port_info_t     *MAC_OS_$PORTP_TABLE[MAC_OS_MAX_PORTS];
mac_os_$port_info_t      MAC_OS_$PORT_TABLE[MAC_OS_MAX_PORTS];
ml_$exclusion_t          MAC_OS_$EXCLUSION;
MODULE_DATA_DEFINE(route_$wired_data_t, ROUTE_$WIRED_DATA, 0x00E26EE4);
uint32_t                 NODE_$ME;

static int excl_init_calls;
static int nop_calls;

void ML_$EXCLUSION_INIT(ml_$exclusion_t *e) { (void)e; excl_init_calls++; }
void MAC_OS_$NOP(void) { nop_calls++; }

#include "../init.c"

/* ============================================================================
 * Fixture
 * ============================================================================ */

static route_$port_t port0;
static uint8_t       driver_info[0x50];

static void reset_all(void)
{
    memset(MAC_OS_$CHANNEL_TABLE, 0, sizeof(MAC_OS_$CHANNEL_TABLE));
    memset(MAC_OS_$PORT_PKT_TABLES, 0, sizeof(MAC_OS_$PORT_PKT_TABLES));
    memset(MAC_OS_$PORTP_TABLE, 0, sizeof(MAC_OS_$PORTP_TABLE));
    memset(MAC_OS_$PORT_TABLE, 0, sizeof(MAC_OS_$PORT_TABLE));
    memset(ROUTE_$WIRED_DATA.portp, 0, sizeof(ROUTE_$WIRED_DATA.portp));
    memset(&port0, 0, sizeof(port0));
    memset(driver_info, 0, sizeof(driver_info));
    excl_init_calls = 0;
    nop_calls = 0;
    /*
     * route_$port_t.driver_info is a 32-bit target VA, and VA zero is nil, so
     * bias the host arena to keep the driver record's VA non-zero.
     */
    ARCH_HOST_VA_BASE = (uintptr_t)driver_info - 0x1000;
    NODE_$ME = 0x0007ABCDu;
}

/* ============================================================================
 * The flag bits
 * ============================================================================ */

TEST(clears_only_word_bits_8_and_9) {
    int i;

    reset_all();
    for (i = 0; i < MAC_OS_MAX_CHANNELS; i++) {
        MAC_OS_$CHANNEL_TABLE[i].flags = 0xFFFF;
    }

    MAC_OS_$INIT();

    for (i = 0; i < MAC_OS_MAX_CHANNELS; i++) {
        /* 0xFFFF minus PROMISCUOUS (0x0100) and IN_USE (0x0200) */
        ASSERT_EQ(0xFCFFu, MAC_OS_$CHANNEL_TABLE[i].flags);
    }
}

TEST(low_byte_of_the_flags_word_is_untouched) {
    reset_all();
    MAC_OS_$CHANNEL_TABLE[0].flags = 0x0303;
    MAC_OS_$INIT();
    /* bits 0 and 1 survive; bits 8 and 9 are gone */
    ASSERT_EQ(0x0003u, MAC_OS_$CHANNEL_TABLE[0].flags);
}

TEST(constants_name_the_right_bits) {
    ASSERT_EQ(0x0100u, MAC_OS_CHANNEL_PROMISCUOUS);
    ASSERT_EQ(0x0200u, MAC_OS_CHANNEL_IN_USE);
    ASSERT_EQ(0xFC00u, MAC_OS_CHANNEL_OWNER_MASK);
    ASSERT_EQ(10u,     MAC_OS_CHANNEL_OWNER_SHIFT);
    /*
     * source-ytxx: callback and driver_info are 32-bit target VAs now, so the
     * whole entry holds its image layout on the host too.
     */
    ASSERT_EQ(0x14u, sizeof(mac_os_$channel_t));
    ASSERT_EQ(0x00u, offsetof(mac_os_$channel_t, callback));
    ASSERT_EQ(0x04u, offsetof(mac_os_$channel_t, driver_info));
    ASSERT_EQ(0x08u, offsetof(mac_os_$channel_t, socket));
    ASSERT_EQ(0x0Au, offsetof(mac_os_$channel_t, port_index));
    ASSERT_EQ(0x0Cu, offsetof(mac_os_$channel_t, callback_data));
    ASSERT_EQ(0x0Eu, offsetof(mac_os_$channel_t, line_number));
    ASSERT_EQ(0x10u, offsetof(mac_os_$channel_t, header_size));
    ASSERT_EQ(0x12u, offsetof(mac_os_$channel_t, flags));
}

TEST(channel_slots_are_reset) {
    int i;

    reset_all();
    for (i = 0; i < MAC_OS_MAX_CHANNELS; i++) {
        MAC_OS_$CHANNEL_TABLE[i].socket      = 0x1234;
        MAC_OS_$CHANNEL_TABLE[i].line_number = 0x5678;
        MAC_OS_$CHANNEL_TABLE[i].driver_info = ARCH_PTR_TO_VA(driver_info);
        MAC_OS_$CHANNEL_TABLE[i].callback    = ARCH_PTR_TO_VA(driver_info);
    }

    MAC_OS_$INIT();

    for (i = 0; i < MAC_OS_MAX_CHANNELS; i++) {
        ASSERT_EQ(MAC_OS_CHANNEL_NO_SOCKET, MAC_OS_$CHANNEL_TABLE[i].socket);
        ASSERT_EQ(0u, MAC_OS_$CHANNEL_TABLE[i].line_number);
        ASSERT_EQ(0u, MAC_OS_$CHANNEL_TABLE[i].driver_info);
        ASSERT_EQ(0u, MAC_OS_$CHANNEL_TABLE[i].callback);
    }
}

TEST(the_eleventh_slot_is_left_alone) {
    /* the loop is "moveq #0x9,D0" plus dbf - ten passes, not eleven */
    reset_all();
    MAC_OS_$CHANNEL_TABLE[MAC_OS_MAX_CHANNELS].flags  = 0xFFFF;
    MAC_OS_$CHANNEL_TABLE[MAC_OS_MAX_CHANNELS].socket = 0x1234;
    MAC_OS_$INIT();
    ASSERT_EQ(0xFFFFu, MAC_OS_$CHANNEL_TABLE[MAC_OS_MAX_CHANNELS].flags);
    ASSERT_EQ(0x1234u, MAC_OS_$CHANNEL_TABLE[MAC_OS_MAX_CHANNELS].socket);
}

/* ============================================================================
 * The port loop
 * ============================================================================ */

TEST(port_tables_and_pointers) {
    int i;

    reset_all();
    MAC_OS_$INIT();

    ASSERT_EQ(1, excl_init_calls);
    for (i = 0; i < MAC_OS_MAX_PORTS; i++) {
        ASSERT_EQ(0u, MAC_OS_$PORT_PKT_TABLES[i].entry_count);
        ASSERT_EQ(1u, MAC_OS_$PORT_TABLE[i].version);
        ASSERT_EQ(0u, MAC_OS_$PORT_TABLE[i].config);
        ASSERT_EQ((uintptr_t)&MAC_OS_$PORT_TABLE[i],
                  (uintptr_t)MAC_OS_$PORTP_TABLE[i]);
    }
    /* no configured port, so the driver block is never touched */
    ASSERT_EQ(0, nop_calls);
}

TEST(configured_port_gets_its_addresses) {
    reset_all();
    ROUTE_$WIRED_DATA.portp[0] = &port0;
    port0.driver_info = ARCH_PTR_TO_VA(driver_info);
    port0.network = 0xFEEDFACEu;
    *(uint16_t *)&driver_info[MAC_OS_DRIVER_MTU_OFFSET] = 0x0400;

    MAC_OS_$INIT();

    ASSERT_EQ(1, nop_calls);
    ASSERT_EQ(0x0400u, MAC_OS_$PORT_TABLE[0].mtu);

    /* 0x00E2F590: the two words at +0x04 are both 1 */
    ASSERT_EQ(0x00010001u,
              *(uint32_t *)((uint8_t *)&port0 + ROUTE_PORT_LINK_ID_OFFSET));

    /* 0x00E2F59C..: the two-word link address {2, node_hi, node_lo} */
    {
        mac_os_$link_addr_t *la = (mac_os_$link_addr_t *)
            ((uint8_t *)&port0 + ROUTE_PORT_LINK_ADDR_OFFSET);

        ASSERT_EQ(2u,      la->n_words);
        ASSERT_EQ(0x0007u, la->addr[0]);    /* (NODE_$ME >> 16) & 0xF */
        ASSERT_EQ(0xABCDu, la->addr[1]);    /* NODE_$ME & 0xFFFF */
    }

    /* 0x00E2F5C0..: 08-00-1E-07-AB-CD with socket 0xFFFF */
    ASSERT_EQ(0xFEEDFACEu, port0.xns_addr.network);
    ASSERT_EQ(0x0800u,     port0.xns_addr.host_hi);
    ASSERT_EQ(0x1E07ABCDu, port0.xns_addr.host_lo);
    ASSERT_EQ(0xFFFFu,     port0.xns_addr.socket);
}

TEST(node_id_high_nibble_is_masked_to_four_bits) {
    reset_all();
    ROUTE_$WIRED_DATA.portp[0] = &port0;
    port0.driver_info = ARCH_PTR_TO_VA(driver_info);
    NODE_$ME = 0xFFFF1234u;         /* only the low nibble of the high word */

    MAC_OS_$INIT();

    ASSERT_EQ(0x1E0F1234u, port0.xns_addr.host_lo);
}

TEST(port_without_a_driver_is_skipped) {
    reset_all();
    ROUTE_$WIRED_DATA.portp[0] = &port0;
    port0.driver_info = 0;          /* no driver record */
    port0.network = 0xFEEDFACEu;

    MAC_OS_$INIT();

    ASSERT_EQ(0, nop_calls);
    ASSERT_EQ(0u, port0.xns_addr.network);
    ASSERT_EQ(0u, MAC_OS_$PORT_TABLE[0].mtu);
}

int main(void)
{
    printf("MAC_OS_$INIT tests\n");
    RUN_TEST(clears_only_word_bits_8_and_9);
    RUN_TEST(low_byte_of_the_flags_word_is_untouched);
    RUN_TEST(constants_name_the_right_bits);
    RUN_TEST(channel_slots_are_reset);
    RUN_TEST(the_eleventh_slot_is_left_alone);
    RUN_TEST(port_tables_and_pointers);
    RUN_TEST(configured_port_gets_its_addresses);
    RUN_TEST(node_id_high_nibble_is_masked_to_four_bits);
    RUN_TEST(port_without_a_driver_is_skipped);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
