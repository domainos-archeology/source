/*
 * UID_$HASH - Hash a UID for table indexing
 *
 * Computes a hash value from a UID for use in hash tables.
 * The algorithm XORs the high and low words, then XORs
 * the upper and lower 16-bit halves together to produce
 * a 16-bit hash value. This is then divided by the table
 * size to produce both a quotient and remainder.
 *
 * Parameters:
 *   uid - Pointer to the UID to hash
 *   table_size - Pointer to the hash table size (divisor)
 *
 * Returns:
 *   Packed result: high word = remainder (index), low word = quotient
 *   Note: M68K DIVU leaves quotient in low word, remainder in high word,
 *   then SWAP reverses this.
 *
 * Original address: 0x00e17360
 */

#include "uid.h"

uint32_t UID_$HASH(uid_t *uid, uint16_t *table_size)
{
    uint32_t hash;
    uint16_t divisor;
    uint16_t quotient;
    uint16_t remainder;

    /* XOR high and low words together */
    hash = uid->high ^ uid->low;

    /* XOR upper and lower 16-bit halves to get 16-bit hash */
    hash = (uint16_t)((hash >> 16) ^ (hash & 0xFFFF));

    divisor = *table_size;

    /* Divide to get quotient and remainder */
    quotient = (uint16_t)(hash / divisor);
    remainder = (uint16_t)(hash % divisor);

    /*
     * Assembly does DIVU (quotient in low, remainder in high) then SWAP
     * Result: high word = remainder (hash index), low word = quotient
     */
    return ((uint32_t)remainder << 16) | quotient;
}
