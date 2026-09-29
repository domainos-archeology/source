/*
 * Tests for FIM_$ACKNOWLEDGE (0x00E0A96C).  Includes the real
 * fim/acknowledge.c with EC_$ADVANCE mocked.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "base/base.h"
#include "fim/fim.h"
#include "proc1/proc1.h"
#include "ec/ec.h"

int __host_intr_disable_count = 0;

#define AS_N 8
uint16_t PROC1_$AS_ID;
MODULE_DATA_DEFINE(fim_$wired_data_t, FIM_$WIRED_DATA, 0x00E21FE6);

static int n_advance; static ec_$eventcount_t *advanced;
void EC_$ADVANCE(ec_$eventcount_t *ec) { n_advance++; advanced = ec; }

#include "../acknowledge.c"

static int tests_failed;
#define ASSERT_EQ(a, b) do { if ((unsigned long long)(a) != (unsigned long long)(b)) { \
    printf("  FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); tests_failed++; } } while (0)

int main(void)
{
    unsigned i;
    for (i = 0; i < AS_N; i++) {
        FIM_$WIRED_DATA.quit_ec[i].value = (int32_t)(0x100 + i);
        FIM_$WIRED_DATA.quit_value[i] = 0xEEEE;
        FIM_$WIRED_DATA.quit_inh[i] = -1;
    }

    /* 0x00E0A994 / 0x00E0A9A6 / 0x00E0A9B4 for AS 5 */
    PROC1_$AS_ID = 5;
    FIM_$ACKNOWLEDGE();
    ASSERT_EQ(FIM_$WIRED_DATA.quit_value[5], 0x105);
    ASSERT_EQ(FIM_$WIRED_DATA.quit_value[4], 0xEEEE);
    ASSERT_EQ(FIM_$WIRED_DATA.quit_value[6], 0xEEEE);
    ASSERT_EQ(FIM_$WIRED_DATA.quit_inh[5], 0);
    ASSERT_EQ((uint8_t)FIM_$WIRED_DATA.quit_inh[4], 0xFF);
    ASSERT_EQ(n_advance, 1);
    ASSERT_EQ((uintptr_t)advanced, (uintptr_t)&FIM_$WIRED_DATA.deliv_ec[5]);
    /* the quit eventcount itself is untouched */
    ASSERT_EQ(FIM_$WIRED_DATA.quit_ec[5].value, 0x105);

    printf("test_acknowledge: %d failed\n", tests_failed);
    return tests_failed ? 1 : 0;
}
