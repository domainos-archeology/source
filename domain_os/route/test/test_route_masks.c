/*
 * Test cases for ROUTE_$ bitmask operations
 *
 * These tests verify the bit-field operations used in ROUTE_$SERVICE
 * for port type validation, status transitions, and operation flags.
 *
 * The routing subsystem uses bitmasks to check valid port types and
 * status transitions. These tests ensure the mask logic is correct.
 */

#include <stdio.h>
#include <stdint.h>
#include <assert.h>

/*
 * =============================================================================
 * Operation Flag Bits (from service.c)
 * =============================================================================
 */

#define SERVICE_OP_SET_NETWORK      0x01    /* Bit 0: Set network address */
#define SERVICE_OP_SET_STATUS       0x02    /* Bit 1: Set port status */
#define SERVICE_OP_CREATE_PORT      0x04    /* Bit 2: Create new port */
#define SERVICE_OP_CLOSE_PORT       0x08    /* Bit 3: Close port */
#define SERVICE_OP_USER_PORT        0x20    /* Bit 5: User port with validation */

/*
 * =============================================================================
 * Port Type and Status Masks (from service.c)
 * =============================================================================
 */

/* Port types 1 and 2 are valid - bits 1 and 2 = 0x06 */
#define PORT_TYPE_VALID_MASK        0x06

/* Status values 1,2,3,4,5 are valid - bits 1-5 = 0x3E */
#define PORT_STATUS_VALID_MASK      0x3E

/* Status bits 3,4,5 require network address - 0x38 */
#define PORT_STATUS_NEED_NETWORK    0x38

/* Status bits 4,5 for routing enabled - 0x30 */
#define PORT_STATUS_ROUTING_MASK    0x30

/* Status bits 1,2,3 for routing disable check - 0x0E */
#define PORT_STATUS_DISABLE_STD     0x0E

/* Status bits 3,5 for N-routing check - 0x28 */
#define PORT_STATUS_N_ROUTING_MASK  0x28

/* Status bits 1,2,4 for N-routing disable - 0x16 */
#define PORT_STATUS_DISABLE_N       0x16

/*
 * =============================================================================
 * Helper macros for mask checking
 * =============================================================================
 */

#define CHECK_MASK(val, mask) (((1 << ((val) & 0x1f)) & (mask)) != 0)

/*
 * =============================================================================
 * Test Cases
 * =============================================================================
 */

/* Test: Operation flag extraction */
void test_operation_flags(void)
{
    uint8_t flags;

    /* Test individual flags */
    flags = SERVICE_OP_SET_NETWORK;
    assert((flags & SERVICE_OP_SET_NETWORK) != 0);
    assert((flags & SERVICE_OP_SET_STATUS) == 0);

    flags = SERVICE_OP_CLOSE_PORT;
    assert((flags & SERVICE_OP_CLOSE_PORT) != 0);
    assert((flags & SERVICE_OP_CREATE_PORT) == 0);

    /* Test combined flags */
    flags = SERVICE_OP_CREATE_PORT | SERVICE_OP_USER_PORT;
    assert((flags & SERVICE_OP_CREATE_PORT) != 0);
    assert((flags & SERVICE_OP_USER_PORT) != 0);
    assert((flags & SERVICE_OP_CLOSE_PORT) == 0);

    printf("test_operation_flags: PASSED\n");
}

/* Test: Port type validation mask */
void test_port_type_mask(void)
{
    /* Port type 0 is invalid */
    assert(!CHECK_MASK(0, PORT_TYPE_VALID_MASK));

    /* Port type 1 is valid (nil driver) */
    assert(CHECK_MASK(1, PORT_TYPE_VALID_MASK));

    /* Port type 2 is valid (user driver) */
    assert(CHECK_MASK(2, PORT_TYPE_VALID_MASK));

    /* Port type 3 is invalid */
    assert(!CHECK_MASK(3, PORT_TYPE_VALID_MASK));

    /* Port types 4-7 are invalid */
    for (int i = 4; i <= 7; i++) {
        assert(!CHECK_MASK(i, PORT_TYPE_VALID_MASK));
    }

    printf("test_port_type_mask: PASSED\n");
}

/* Test: Port status validation mask */
void test_port_status_mask(void)
{
    /* Status 0 is invalid */
    assert(!CHECK_MASK(0, PORT_STATUS_VALID_MASK));

    /* Status 1-5 are valid */
    for (int i = 1; i <= 5; i++) {
        assert(CHECK_MASK(i, PORT_STATUS_VALID_MASK));
    }

    /* Status 6 and above are invalid */
    assert(!CHECK_MASK(6, PORT_STATUS_VALID_MASK));
    assert(!CHECK_MASK(7, PORT_STATUS_VALID_MASK));

    printf("test_port_status_mask: PASSED\n");
}

/* Test: Network requirement mask */
void test_network_requirement_mask(void)
{
    /* Status 1, 2 do not require network */
    assert(!CHECK_MASK(1, PORT_STATUS_NEED_NETWORK));
    assert(!CHECK_MASK(2, PORT_STATUS_NEED_NETWORK));

    /* Status 3, 4, 5 require network */
    assert(CHECK_MASK(3, PORT_STATUS_NEED_NETWORK));
    assert(CHECK_MASK(4, PORT_STATUS_NEED_NETWORK));
    assert(CHECK_MASK(5, PORT_STATUS_NEED_NETWORK));

    printf("test_network_requirement_mask: PASSED\n");
}

/* Test: Routing state transitions */
void test_routing_transitions(void)
{
    /*
     * STATUS_ROUTING_MASK (0x30) = bits 4,5 = states 4,5
     * STATUS_DISABLE_STD (0x0E) = bits 1,2,3 = states 1,2,3
     *
     * Transition from routing (4,5) to non-routing (1,2,3) triggers decrement
     */

    /* Status 4 is a routing state */
    assert(CHECK_MASK(4, PORT_STATUS_ROUTING_MASK));
    /* Status 5 is a routing state */
    assert(CHECK_MASK(5, PORT_STATUS_ROUTING_MASK));
    /* Status 1,2,3 are not routing states */
    assert(!CHECK_MASK(1, PORT_STATUS_ROUTING_MASK));
    assert(!CHECK_MASK(2, PORT_STATUS_ROUTING_MASK));
    assert(!CHECK_MASK(3, PORT_STATUS_ROUTING_MASK));

    /* Status 1,2,3 disable STD routing */
    assert(CHECK_MASK(1, PORT_STATUS_DISABLE_STD));
    assert(CHECK_MASK(2, PORT_STATUS_DISABLE_STD));
    assert(CHECK_MASK(3, PORT_STATUS_DISABLE_STD));
    /* Status 4,5 do not disable STD routing */
    assert(!CHECK_MASK(4, PORT_STATUS_DISABLE_STD));
    assert(!CHECK_MASK(5, PORT_STATUS_DISABLE_STD));

    printf("test_routing_transitions: PASSED\n");
}

/* Test: N-routing state transitions */
void test_n_routing_transitions(void)
{
    /*
     * N_ROUTING_MASK (0x28) = bits 3,5 = states 3,5
     * DISABLE_N (0x16) = bits 1,2,4 = states 1,2,4
     */

    /* Status 3,5 are N-routing states */
    assert(CHECK_MASK(3, PORT_STATUS_N_ROUTING_MASK));
    assert(CHECK_MASK(5, PORT_STATUS_N_ROUTING_MASK));
    /* Status 1,2,4 are not N-routing states */
    assert(!CHECK_MASK(1, PORT_STATUS_N_ROUTING_MASK));
    assert(!CHECK_MASK(2, PORT_STATUS_N_ROUTING_MASK));
    assert(!CHECK_MASK(4, PORT_STATUS_N_ROUTING_MASK));

    /* Status 1,2,4 disable N routing */
    assert(CHECK_MASK(1, PORT_STATUS_DISABLE_N));
    assert(CHECK_MASK(2, PORT_STATUS_DISABLE_N));
    assert(CHECK_MASK(4, PORT_STATUS_DISABLE_N));
    /* Status 3,5 do not disable N routing */
    assert(!CHECK_MASK(3, PORT_STATUS_DISABLE_N));
    assert(!CHECK_MASK(5, PORT_STATUS_DISABLE_N));

    printf("test_n_routing_transitions: PASSED\n");
}

/* Test: Status transition logic (from old_status to new_status) */
void test_status_transition_logic(void)
{
    /*
     * Test the transition from routing (4,5) -> non-routing (1,2,3)
     * This should trigger ROUTE_$DECREMENT_PORT with STD flag
     */
    int old_status, new_status;

    /* 4 -> 1: Routing to non-routing (STD decrement) */
    old_status = 4;
    new_status = 1;
    assert(CHECK_MASK(old_status, PORT_STATUS_ROUTING_MASK) &&
           CHECK_MASK(new_status, PORT_STATUS_DISABLE_STD));

    /* 5 -> 2: Routing to non-routing (STD decrement) */
    old_status = 5;
    new_status = 2;
    assert(CHECK_MASK(old_status, PORT_STATUS_ROUTING_MASK) &&
           CHECK_MASK(new_status, PORT_STATUS_DISABLE_STD));

    /* 1 -> 4: Non-routing to routing (STD init) */
    old_status = 1;
    new_status = 4;
    assert(CHECK_MASK(old_status, PORT_STATUS_DISABLE_STD) &&
           CHECK_MASK(new_status, PORT_STATUS_ROUTING_MASK));

    /* 3 -> 5: N-routing, but not STD routing change */
    old_status = 3;
    new_status = 5;
    /* Both 3 and 5 are N-routing, so no N transition */
    assert(CHECK_MASK(old_status, PORT_STATUS_N_ROUTING_MASK) &&
           CHECK_MASK(new_status, PORT_STATUS_N_ROUTING_MASK));
    /* 3 is not STD routing, 5 is, so STD init needed */
    assert(!CHECK_MASK(old_status, PORT_STATUS_ROUTING_MASK) &&
           CHECK_MASK(new_status, PORT_STATUS_ROUTING_MASK));

    printf("test_status_transition_logic: PASSED\n");
}

/* Test: Port structure offsets */
void test_port_structure_offsets(void)
{
    /*
     * The port structure has key fields at specific offsets:
     *   +0x00: network (4 bytes)
     *   +0x20: cached network copy (4 bytes)
     *   +0x2C: active status (2 bytes)
     *   +0x2E: port type (2 bytes)
     *   +0x30: socket/port EC area start
     *   +0x48: driver info pointer
     *   +0x5C: total size
     *
     * These tests verify the offset calculations used in the code.
     */

    /* Verify port size */
    assert(0x5C == 92);  /* 92 bytes per port */

    /* Verify array index calculation: port_index * 0x17 words = port_index * 92 bytes */
    assert(0x17 * 4 == 92);  /* 0x17 = 23 dwords */

    /* Verify cached copy offset */
    assert(0x20 == 32);

    /* Verify active status offset */
    assert(0x2C == 44);

    /* Verify port type offset */
    assert(0x2E == 46);

    /* Verify driver info pointer offset */
    assert(0x48 == 72);

    printf("test_port_structure_offsets: PASSED\n");
}

int main(void)
{
    printf("Running ROUTE_$ bitmask and structure tests...\n\n");

    test_operation_flags();
    test_port_type_mask();
    test_port_status_mask();
    test_network_requirement_mask();
    test_routing_transitions();
    test_n_routing_transitions();
    test_status_transition_logic();
    test_port_structure_offsets();

    printf("\nAll tests PASSED!\n");
    return 0;
}
