/*
 * ACL_$IMAGE - Create an ACL image
 *
 * Creates an ACL image from the specified source UID. This function
 * wraps the internal image creation logic with cleanup handling and
 * an exclusion lock.
 *
 * Parameters:
 *   source_uid    - Source UID to create image from
 *   buffer_len    - Pointer to buffer length (input/output)
 *   unknown_flag  - Pointer to flag byte
 *   param_4       - Unknown parameter
 *   param_5       - Unknown parameter
 *   param_6       - Unknown parameter
 *   status_ret    - Output status code
 *
 * Original address: 0x00E47DF6
 */

#include "acl/acl_internal.h"
#include "fim/fim.h"

/* Forward declaration for internal image helper */
void acl_$image_internal(void *source_uid, int16_t buffer_len, int8_t unknown_flag,
                         void *param_4, void *param_5, void *param_6,
                         void *local_flag, status_$t *status);

void ACL_$IMAGE(void *source_uid, int16_t *buffer_len, int8_t *unknown_flag,
                void *param_4, void *param_5, void *param_6, status_$t *status_ret)
{
    uint8_t cleanup_buf[24];
    status_$t status;
    uint8_t local_flag[2];

    /* Acquire lock #10 */
    ML_$LOCK(10);

    /* Set up cleanup handler */
    status = FIM_$CLEANUP(cleanup_buf);

    if (status == status_$cleanup_handler_set) {
        /* Call internal image creation function */
        acl_$image_internal(source_uid, *buffer_len, *unknown_flag,
                           param_4, param_5, param_6, local_flag, &status);

        /* Release cleanup handler */
        FIM_$RLS_CLEANUP(cleanup_buf);
    } else {
        /* Cleanup handler failed - pop signal */
        FIM_$POP_SIGNAL(cleanup_buf);
    }

    /* Release lock #10 */
    ML_$UNLOCK(10);

    *status_ret = status;
}
