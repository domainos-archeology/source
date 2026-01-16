/*
 * NAMEQ - Pascal String Comparison for Pathnames
 *
 * Compares two Pascal-style strings (length-prefixed) for equality,
 * ignoring trailing spaces in either string. This is the standard
 * comparison used for pathname components in Domain/OS.
 *
 * Original address: 0x00e49eac
 * Original size: 160 bytes
 */

#include "name/name_internal.h"

/*
 * NAMEQ - Compare two Pascal-style strings for equality
 *
 * Algorithm:
 * 1. If either length is 0, return false
 * 2. Compare the common prefix (min of both lengths)
 * 3. If lengths differ, verify the longer string has only trailing spaces
 *
 * Parameters:
 *   str1 - First string (1-indexed in original, but we use 0-indexed in C)
 *   len1 - Pointer to length of first string
 *   str2 - Second string
 *   len2 - Pointer to length of second string
 *
 * Returns:
 *   true (0xFF) if strings match (ignoring trailing spaces)
 *   false (0) if strings differ
 */
boolean NAMEQ(char *str1, uint16_t *len1, char *str2, uint16_t *len2)
{
    uint16_t length1 = *len1;
    uint16_t length2 = *len2;
    uint16_t min_len;
    uint16_t i;

    /* Both strings must have non-zero length */
    if (length1 == 0 || length2 == 0) {
        return false;
    }

    /* Determine the minimum length to compare */
    min_len = (length1 < length2) ? length1 : length2;

    /* Compare the common prefix character by character */
    for (i = 0; i < min_len; i++) {
        if (str1[i] != str2[i]) {
            return false;
        }
    }

    /* If str1 is longer, check that remaining chars are spaces */
    if (length1 > length2) {
        for (i = length2; i < length1; i++) {
            if (str1[i] != ' ') {
                return false;
            }
        }
    }
    /* If str2 is longer, check that remaining chars are spaces */
    else if (length2 > length1) {
        for (i = length1; i < length2; i++) {
            if (str2[i] != ' ') {
                return false;
            }
        }
    }

    return true;
}
