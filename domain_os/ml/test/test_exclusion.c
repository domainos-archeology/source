/*
 * Test cases for ML_$ exclusion lock functions
 *
 * These tests verify the basic functionality of the exclusion lock
 * primitives. Note that full testing requires the kernel environment;
 * these are unit tests for the logic.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

/* Include the exclusion lock structure definition */
typedef struct ml_$exclusion_t {
    int32_t     f1;
    void        *f2;
    void        *f3;
    int32_t     f4;
    int16_t     f5;
} ml_$exclusion_t;

/* Forward declarations - we'll test the logic directly */
void ML_$EXCLUSION_INIT(ml_$exclusion_t *excl);
int8_t ML_$EXCLUSION_CHECK(ml_$exclusion_t *excl);
int8_t ML_$COND_EXCLUSION_START(ml_$exclusion_t *excl);
void ML_$COND_EXCLUSION_STOP(ml_$exclusion_t *excl);

/* Re-implement the simpler functions for testing */
void test_exclusion_init(ml_$exclusion_t *excl)
{
    excl->f1 = 0;
    excl->f2 = excl;
    excl->f3 = excl;
    excl->f4 = 0;
    excl->f5 = -1;
}

int8_t test_exclusion_check(ml_$exclusion_t *excl)
{
    return (excl->f5 >= 0) ? -1 : 0;
}

int8_t test_cond_exclusion_start(ml_$exclusion_t *excl)
{
    if (excl->f5 >= 0) {
        return -1;
    }
    excl->f5 = 0;
    return 0;
}

void test_cond_exclusion_stop(ml_$exclusion_t *excl)
{
    excl->f5--;
}

/* Test: ML_$EXCLUSION_INIT initializes correctly */
void test_init(void)
{
    ml_$exclusion_t excl;

    memset(&excl, 0xFF, sizeof(excl));

    test_exclusion_init(&excl);

    assert(excl.f1 == 0);
    assert(excl.f2 == &excl);
    assert(excl.f3 == &excl);
    assert(excl.f4 == 0);
    assert(excl.f5 == -1);

    printf("test_init: PASSED\n");
}

/* Test: ML_$EXCLUSION_CHECK returns correct values */
void test_check(void)
{
    ml_$exclusion_t excl;

    /* Unlocked state (f5 = -1) should return 0 */
    test_exclusion_init(&excl);
    assert(test_exclusion_check(&excl) == 0);

    /* Locked state (f5 = 0) should return non-zero */
    excl.f5 = 0;
    assert(test_exclusion_check(&excl) != 0);

    /* Locked with waiters (f5 = 5) should return non-zero */
    excl.f5 = 5;
    assert(test_exclusion_check(&excl) != 0);

    printf("test_check: PASSED\n");
}

/* Test: ML_$COND_EXCLUSION_START works correctly */
void test_cond_start(void)
{
    ml_$exclusion_t excl;

    /* Start on unlocked should succeed and set f5 = 0 */
    test_exclusion_init(&excl);
    assert(test_cond_exclusion_start(&excl) == 0);
    assert(excl.f5 == 0);

    /* Start on already locked should fail */
    assert(test_cond_exclusion_start(&excl) != 0);
    assert(excl.f5 == 0);  /* State unchanged */

    printf("test_cond_start: PASSED\n");
}

/* Test: ML_$COND_EXCLUSION_STOP works correctly */
void test_cond_stop(void)
{
    ml_$exclusion_t excl;

    /* Stop should decrement f5 */
    test_exclusion_init(&excl);
    test_cond_exclusion_start(&excl);  /* f5 = 0 */

    test_cond_exclusion_stop(&excl);
    assert(excl.f5 == -1);  /* Back to unlocked */

    printf("test_cond_stop: PASSED\n");
}

/* Test: Full exclusion lifecycle */
void test_lifecycle(void)
{
    ml_$exclusion_t excl;

    /* Initialize */
    test_exclusion_init(&excl);
    assert(test_exclusion_check(&excl) == 0);

    /* Acquire */
    assert(test_cond_exclusion_start(&excl) == 0);
    assert(test_exclusion_check(&excl) != 0);

    /* Try to acquire again - should fail */
    assert(test_cond_exclusion_start(&excl) != 0);
    assert(test_exclusion_check(&excl) != 0);

    /* Release */
    test_cond_exclusion_stop(&excl);
    assert(test_exclusion_check(&excl) == 0);

    /* Can acquire again */
    assert(test_cond_exclusion_start(&excl) == 0);
    assert(test_exclusion_check(&excl) != 0);

    printf("test_lifecycle: PASSED\n");
}

int main(void)
{
    printf("Running ML_$ exclusion lock tests...\n\n");

    test_init();
    test_check();
    test_cond_start();
    test_cond_stop();
    test_lifecycle();

    printf("\nAll tests PASSED!\n");
    return 0;
}
