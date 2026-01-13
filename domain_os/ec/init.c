/*
 * EC_$INIT - Initialize an event count
 *
 * Sets value to 0 and initializes empty waiter list (circular,
 * pointing to the eventcount itself).
 *
 * Parameters:
 *   ec - Pointer to eventcount structure
 *
 * Original address: 0x00e151fe
 */

#include "ec.h"

void EC_$INIT(ec_$eventcount_t *ec)
{
    ec->value = 0;
    ec->waiter_list_head = (ec_$eventcount_waiter_t *)ec;
    ec->waiter_list_tail = (ec_$eventcount_waiter_t *)ec;
}
