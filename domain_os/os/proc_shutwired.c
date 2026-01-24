/*
 * OS_PROC_SHUTWIRED - Translate internal status codes to external format
 *
 * This function translates internal ACL/file subsystem status codes to
 * their external equivalents for wired shutdown operations. It maps:
 *
 *   0x230001 (no_right_to_perform_operation)        -> 0xF0010 (no_rights)
 *   0x230002 (insufficient_rights_to_perform_operation) -> 0xF0011 (insufficient_rights)
 *   0x230004 (wrong_type)                           -> 0xF0012 (file_wrong_type)
 *   0x230007 (acl_on_different_volume)              -> 0xF0013 (file_objects_on_different_volumes)
 *
 * For any other status code except 0xF0001, it sets the high bit (0x80)
 * of the status byte to mark it as an internal/special error.
 *
 * Original address: 0x00E5D050
 * Size: 88 bytes
 */

#include "os/os_internal.h"

/* Internal ACL status codes (module 0x23) */
#define status_$acl_no_right_to_perform_operation           0x00230001
#define status_$acl_insufficient_rights_to_perform_operation 0x00230002
#define status_$acl_wrong_type                              0x00230004
#define status_$acl_on_different_volume                     0x00230007

/* External file status codes (module 0x0F) - mapped versions */
#define status_$no_rights                                   0x000F0010
#define file_$wrong_type                             0x000F0012

/* Special status code that should not be modified */
#define status_$special_passthrough                         0x000F0001

/*
 * OS_PROC_SHUTWIRED - Translate status codes for shutdown wired operations
 *
 * Parameters:
 *   status_ret - Pointer to status code (in/out)
 *               On input: internal status code
 *               On output: translated external status code
 */
void OS_PROC_SHUTWIRED(status_$t *status_ret)
{
    if (*status_ret == status_$acl_no_right_to_perform_operation) {
        /* no_right_to_perform_operation -> no_rights */
        *status_ret = status_$no_rights;
    }
    else if (*status_ret == status_$acl_insufficient_rights_to_perform_operation) {
        /* insufficient_rights_to_perform_operation -> insufficient_rights */
        *status_ret = status_$insufficient_rights;
    }
    else if (*status_ret == status_$acl_wrong_type) {
        /* wrong_type -> file_wrong_type */
        *status_ret = file_$wrong_type;
    }
    else if (*status_ret == status_$acl_on_different_volume) {
        /* acl_on_different_volume -> file_objects_on_different_volumes */
        *status_ret = file_$objects_on_different_volumes;
    }
    else if (*status_ret != status_$special_passthrough) {
        /* For all other codes except 0xF0001, set high bit to mark as internal */
        *(uint8_t *)status_ret |= 0x80;
    }
}
