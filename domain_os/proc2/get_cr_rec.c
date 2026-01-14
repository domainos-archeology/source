/*
 * PROC2_$GET_CR_REC - Get creation record
 *
 * Returns the parent UID and process UID associated with an eventcount.
 * The EC handle is converted to an EC1 address, which is then used to
 * calculate the process table index.
 *
 * Parameters:
 *   ec_handle   - EC2 eventcount handle
 *   parent_uid  - Pointer to receive parent UID (8 bytes)
 *   proc_uid    - Pointer to receive process UID (8 bytes)
 *   status_ret  - Pointer to receive status
 *
 * Returns uid_not_found if process is not valid or zombie.
 *
 * Original address: 0x00e4015c
 */

#include "proc2.h"
#include "ec/ec.h"

/* EC1 eventcount entry size for calculating process index */
#define EC1_ENTRY_SIZE      0x18        /* 24 bytes per entry */

void PROC2_$GET_CR_REC(uint32_t *ec_handle, uid_$t *parent_uid, uid_$t *proc_uid,
                       status_$t *status_ret)
{
    ec2_$eventcount_t ec2;
    ec_$eventcount_t *ec1_addr;
    status_$t status;
    int16_t proc_idx;
    proc2_info_t *entry;

    /* Copy handle value into ec2 structure */
    ec2.value = (int32_t)*ec_handle;
    ec2.awaiters = 0;

    /* Convert EC2 handle to EC1 address */
    ec1_addr = EC2_$GET_EC1_ADDR(&ec2, &status);

    /* Calculate process index from EC1 address:
     * index = ((ec1_addr - EC1_ARRAY_BASE) / EC1_ENTRY_SIZE) + 1
     */
    proc_idx = (int16_t)(((uintptr_t)ec1_addr - EC1_ARRAY_BASE) / EC1_ENTRY_SIZE) + 1;

    if (status != status_$ok) {
        *status_ret = status_$proc2_uid_not_found;
        return;
    }

    /* Get process entry */
    entry = P2_INFO_ENTRY(proc_idx);

    /* Check if process is valid (0x100) or zombie (0x2000) */
    if ((entry->flags & 0x0100) == 0 && (entry->flags & PROC2_FLAG_ZOMBIE) == 0) {
        *status_ret = status_$proc2_uid_not_found;
        return;
    }

    /* Return parent UID from offset 0x40 relative to entry (offset -0xDC from table pointer) */
    /* This is the parent_uid field at offset 0x08 in entry */
    parent_uid->high = *(uint32_t *)((char *)entry + 0x08);
    parent_uid->low = *(uint32_t *)((char *)entry + 0x0C);

    /* Return process UID */
    proc_uid->high = entry->uid.high;
    proc_uid->low = entry->uid.low;

    *status_ret = status_$ok;
}
