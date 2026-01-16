/*
 * Test for NAMEQ - Pascal String Comparison
 *
 * Tests the NAMEQ function which compares two Pascal-style strings
 * for equality, ignoring trailing spaces.
 */

#include <stdio.h>
#include <string.h>

/* Minimal type definitions for testing */
typedef unsigned short uint16_t;
typedef char boolean;
#define true  ((boolean)-1)
#define false ((boolean)0)

/* Function under test */
boolean NAMEQ(char *str1, uint16_t *len1, char *str2, uint16_t *len2);

/* Test helper */
static int test_count = 0;
static int pass_count = 0;

static void test_result(const char *name, boolean expected, boolean actual)
{
    test_count++;
    if ((expected && actual) || (!expected && !actual)) {
        pass_count++;
        printf("  PASS: %s\n", name);
    } else {
        printf("  FAIL: %s (expected %d, got %d)\n", name,
               expected ? 1 : 0, actual ? 1 : 0);
    }
}

int main(void)
{
    uint16_t len1, len2;

    printf("Testing NAMEQ...\n");

    /* Test 1: Equal strings of same length */
    {
        char s1[] = "hello";
        char s2[] = "hello";
        len1 = 5;
        len2 = 5;
        test_result("equal strings same length",
                    true, NAMEQ(s1, &len1, s2, &len2));
    }

    /* Test 2: Different strings of same length */
    {
        char s1[] = "hello";
        char s2[] = "world";
        len1 = 5;
        len2 = 5;
        test_result("different strings same length",
                    false, NAMEQ(s1, &len1, s2, &len2));
    }

    /* Test 3: String with trailing spaces vs shorter string */
    {
        char s1[] = "foo   ";  /* "foo" with trailing spaces */
        char s2[] = "foo";
        len1 = 6;
        len2 = 3;
        test_result("trailing spaces ignored (str1 longer)",
                    true, NAMEQ(s1, &len1, s2, &len2));
    }

    /* Test 4: Shorter string vs string with trailing spaces */
    {
        char s1[] = "bar";
        char s2[] = "bar   ";
        len1 = 3;
        len2 = 6;
        test_result("trailing spaces ignored (str2 longer)",
                    true, NAMEQ(s1, &len1, s2, &len2));
    }

    /* Test 5: Empty string vs empty string */
    {
        char s1[] = "";
        char s2[] = "";
        len1 = 0;
        len2 = 0;
        test_result("empty strings",
                    false, NAMEQ(s1, &len1, s2, &len2));  /* Both empty returns false */
    }

    /* Test 6: Non-space trailing characters */
    {
        char s1[] = "testX";
        char s2[] = "test";
        len1 = 5;
        len2 = 4;
        test_result("non-space trailing char",
                    false, NAMEQ(s1, &len1, s2, &len2));
    }

    /* Test 7: Single character strings */
    {
        char s1[] = "a";
        char s2[] = "a";
        len1 = 1;
        len2 = 1;
        test_result("single char equal",
                    true, NAMEQ(s1, &len1, s2, &len2));
    }

    /* Test 8: Single character different */
    {
        char s1[] = "a";
        char s2[] = "b";
        len1 = 1;
        len2 = 1;
        test_result("single char different",
                    false, NAMEQ(s1, &len1, s2, &len2));
    }

    /* Test 9: Case sensitivity */
    {
        char s1[] = "Test";
        char s2[] = "test";
        len1 = 4;
        len2 = 4;
        test_result("case sensitive",
                    false, NAMEQ(s1, &len1, s2, &len2));
    }

    /* Test 10: Mixed content with spaces */
    {
        char s1[] = "a b c   ";
        char s2[] = "a b c";
        len1 = 8;
        len2 = 5;
        test_result("mixed content trailing spaces",
                    true, NAMEQ(s1, &len1, s2, &len2));
    }

    printf("\nResults: %d/%d tests passed\n", pass_count, test_count);

    return (pass_count == test_count) ? 0 : 1;
}
