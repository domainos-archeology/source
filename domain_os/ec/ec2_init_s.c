/*
 * EC2_$INIT_S - System initialization for EC2 subsystem
 *
 * Initializes:
 *   - EC1 array (64 entries)
 *   - EC2 waiter table (225 entries)
 *   - PBU pool allocation bitmaps
 *
 * Original address: 0x00e30970
 */

#include "ec_internal.h"

void EC2_$INIT_S(void)
{
    int16_t i;
    ec_$eventcount_t *ec1;
    ec2_waiter_t *waiter;

    /* Initialize EC1 array (64 entries) */
    ec1 = (ec_$eventcount_t *)EC1_ARRAY_BASE;
    for (i = 0; i < 64; i++) {
        EC_$INIT(ec1);
        ec1++;
    }

    /* Initialize EC2 waiter table (225 entries) */
    waiter = (ec2_waiter_t *)EC2_WAITER_TABLE_BASE;
    for (i = 0; i <= 0xE0; i++) {
        waiter->wait_val = 0;
        waiter->proc_id = 0;
        waiter->next = i + 1;  /* Link to next free */
        waiter++;
    }

    /* Initialize global state */
    DAT_00e7caf0 = 0;           /* Clear registration count */
    DAT_00e7cf08 = 1;           /* Free list head = 1 */
    _DAT_00e7cf04 = 1;          /* Max registered index = 1 */
    DAT_00e7cafc = &_DAT_00e7cf04;  /* Pointer to max index */
    DAT_00e7cf00 = 0;           /* Clear allocation bitmap */
    DAT_00e7cefc = 0;           /* Clear pending release bitmap */
}
