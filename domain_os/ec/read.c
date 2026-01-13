/*
 * EC_$READ - Read current eventcount value
 *
 * Parameters:
 *   ec - Pointer to eventcount structure
 *
 * Returns:
 *   Current value of the eventcount
 *
 * Original address: 0x00e15214
 */

#include "ec.h"

int32_t EC_$READ(ec_$eventcount_t *ec)
{
    return ec->value;
}
