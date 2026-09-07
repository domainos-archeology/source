/*
 * Test for LOCATE_SERVER (0x00E4A420) - the REM_NAME module's internal
 * server-location helper.
 *
 * Exercises the four paths of the helper:
 *   1. Recent contact  -> delegate to REM_NAME_$LOCATE_SERVER
 *   2. Stale contact   -> clear heard_from_server, report "cant find helper"
 *   3. No contact, retry budget exhausted (> 3) -> report "cant find helper"
 *   4. No contact, retry allowed -> delegate, and on success record contact
 */

#include <stdio.h>
#include <stdint.h>

/* boolean, status_$t, status_$ok and
 * status_$naming_cant_find_name_server_helper come from their owning headers;
 * do not restate the code or the types here. */
#include "name/name.h"

/* The pieces of rem_name/rem_name_internal.h the function under test uses. */
typedef struct rem_name_data_t {
    uint16_t config[15];
    uint16_t reserved1;
    uint32_t server_timeout;
    uint32_t reserved2;
    uint32_t time_heard_from_server;
    status_$t last_status;
    uint32_t curr_node;
    uint32_t curr_net;
    uint16_t service_delay;
    uint16_t retry_count;
    int8_t   heard_from_server;
} rem_name_data_t;

static rem_name_data_t rem_name_$data;
static uint32_t TIME_$CLOCKH;

/* Stub for the exported entry point LOCATE_SERVER delegates to. */
static int locate_calls;
static status_$t locate_status;
static void REM_NAME_$LOCATE_SERVER(uint32_t *node_ret, uint32_t *net_ret,
                                    status_$t *status_ret)
{
    locate_calls++;
    *node_ret = 0x1234;
    *net_ret = 0;
    *status_ret = locate_status;
}

/* Function under test - include directly, bypassing the header chain */
#define REM_NAME_INTERNAL_H
#include "../locate_server_internal.c"

static int test_count = 0;
static int pass_count = 0;

static void check(const char *what, unsigned long expected, unsigned long actual)
{
    test_count++;
    if (expected == actual) {
        pass_count++;
        printf("  PASS: %s\n", what);
    } else {
        printf("  FAIL: %s (expected 0x%lx, got 0x%lx)\n", what, expected, actual);
    }
}

static void reset(void)
{
    rem_name_$data = (rem_name_data_t){ 0 };
    rem_name_$data.server_timeout = 0x960;
    locate_calls = 0;
    locate_status = status_$ok;
}

int main(void)
{
    uint32_t node, net;
    status_$t status;

    printf("Testing LOCATE_SERVER...\n");

    /* 1. Recent contact: delegates, leaves the contact state alone. */
    reset();
    rem_name_$data.heard_from_server = true;
    rem_name_$data.time_heard_from_server = 2000;
    TIME_$CLOCKH = 1000;                 /* |diff| = 1000 <= 0x960 (2400) */
    status = 0xdeadbeef;
    LOCATE_SERVER(&node, &net, &status);
    check("recent contact delegates", 1, locate_calls);
    check("recent contact status", status_$ok, status);
    check("recent contact node", 0x1234, node);
    check("recent contact still heard", 1, rem_name_$data.heard_from_server != 0);

    /* 1b. The elapsed time is compared as a magnitude: the clock running
     * ahead of the recorded stamp gives the same answer. */
    reset();
    rem_name_$data.heard_from_server = true;
    rem_name_$data.time_heard_from_server = 1000;
    TIME_$CLOCKH = 2000;
    LOCATE_SERVER(&node, &net, &status);
    check("negative delta uses magnitude", 1, locate_calls);

    /* 2. Stale contact: no delegation, contact flag cleared, status recorded. */
    reset();
    rem_name_$data.heard_from_server = true;
    rem_name_$data.time_heard_from_server = 0;
    TIME_$CLOCKH = 0x961;                /* just past server_timeout */
    status = status_$ok;
    LOCATE_SERVER(&node, &net, &status);
    check("stale contact does not delegate", 0, locate_calls);
    check("stale contact status", status_$naming_cant_find_name_server_helper, status);
    check("stale contact last_status", status_$naming_cant_find_name_server_helper,
          rem_name_$data.last_status);
    check("stale contact clears flag", 0, rem_name_$data.heard_from_server != 0);

    /* 3. No contact, retry budget exhausted: > 3 gives up without delegating. */
    reset();
    rem_name_$data.retry_count = 4;
    status = status_$ok;
    LOCATE_SERVER(&node, &net, &status);
    check("retry cap does not delegate", 0, locate_calls);
    check("retry cap status", status_$naming_cant_find_name_server_helper, status);
    check("retry cap leaves counter", 4, rem_name_$data.retry_count);

    /* 3b. A retry_count of exactly 3 is still allowed through. */
    reset();
    rem_name_$data.retry_count = 3;
    LOCATE_SERVER(&node, &net, &status);
    check("retry 3 delegates", 1, locate_calls);
    check("retry 3 bumps counter", 4, rem_name_$data.retry_count);

    /* 4. No contact, delegation succeeds: contact is recorded. */
    reset();
    TIME_$CLOCKH = 0x4242;
    LOCATE_SERVER(&node, &net, &status);
    check("first try delegates", 1, locate_calls);
    check("success sets heard flag", 1, rem_name_$data.heard_from_server != 0);
    check("success stamps clock", 0x4242, rem_name_$data.time_heard_from_server);

    /* 4b. Delegation fails: no contact is recorded. */
    reset();
    locate_status = status_$naming_cant_find_name_server_helper;
    TIME_$CLOCKH = 0x4242;
    LOCATE_SERVER(&node, &net, &status);
    check("failure leaves heard flag", 0, rem_name_$data.heard_from_server != 0);
    check("failure leaves clock", 0, rem_name_$data.time_heard_from_server);
    check("failure bumps counter", 1, rem_name_$data.retry_count);

    printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
