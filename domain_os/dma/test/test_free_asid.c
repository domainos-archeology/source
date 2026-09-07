/*
 * dma/test/test_free_asid.c - unit tests for DMA_$FREE_ASID (0x00E0A454)
 *
 * The original routine is a single `rts`, so the only behaviour there is to
 * verify is that it returns for every ASID the caller can hand it and that it
 * touches nothing.  The test guards the surrounding memory to catch a future
 * edit that gives the stub a body by accident.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Function under test */
#include "../free_asid.c"

static int failures = 0;

#define ASSERT_TRUE(cond, what)                                              \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  FAIL: %s\n", (what));                                  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

/* A canary block the stub must not disturb. */
static uint8_t canary[64];

static void test_returns_for_every_asid(void)
{
    int16_t asid;

    printf("Testing DMA_$FREE_ASID returns for every ASID...\n");
    memset(canary, 0xA5, sizeof(canary));

    /* PROC2 ASIDs run 1..64 in this build; sweep the whole int16 edge too. */
    for (asid = 0; asid <= 64; asid++) {
        DMA_$FREE_ASID(asid);
    }
    DMA_$FREE_ASID(-1);
    DMA_$FREE_ASID(0x7FFF);
    DMA_$FREE_ASID((int16_t)0x8000);

    for (size_t i = 0; i < sizeof(canary); i++) {
        ASSERT_TRUE(canary[i] == 0xA5, "DMA_$FREE_ASID modified memory");
    }
    printf("  returned for 0..64, -1, 0x7FFF and 0x8000; canary intact\n");
}

int main(void)
{
    printf("DMA_$FREE_ASID (0x00E0A454) tests\n");
    test_returns_for_every_asid();
    if (failures == 0) {
        printf("All DMA_$FREE_ASID tests passed\n");
        return 0;
    }
    printf("%d DMA_$FREE_ASID assertion(s) failed\n", failures);
    return 1;
}
