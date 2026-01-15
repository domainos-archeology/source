/*
 * ast_$validate_uid - Validate UID and return "not found" status
 *
 * This function stores the UID and flags to global storage areas
 * (for debugging/error reporting) and returns a "file object not found"
 * status. Used when an operation fails to find a requested object.
 *
 * Original address: 0x00e00be8
 */

#include "ast/ast_internal.h"

/*
 * Global storage for failed UID lookups (for error reporting)
 * At A5+0x478, A5+0x47C, A5+0x480 in original code
 */
#if defined(M68K)
#define AST_$FAILED_UID_HIGH (*(uint32_t *)0xE1E0F8)  /* A5+0x478 */
#define AST_$FAILED_UID_LOW  (*(uint32_t *)0xE1E0FC)  /* A5+0x47C */
#define AST_$FAILED_FLAGS    (*(uint32_t *)0xE1E100)  /* A5+0x480 */
#else
extern uint32_t ast_$failed_uid_high;
extern uint32_t ast_$failed_uid_low;
extern uint32_t ast_$failed_flags;
#define AST_$FAILED_UID_HIGH ast_$failed_uid_high
#define AST_$FAILED_UID_LOW  ast_$failed_uid_low
#define AST_$FAILED_FLAGS    ast_$failed_flags
#endif

status_$t ast_$validate_uid(uid_t *uid, uint32_t flags)
{
    /* Store the UID and flags to global storage for debugging/error reporting */
    AST_$FAILED_UID_HIGH = uid->high;
    AST_$FAILED_UID_LOW = uid->low;
    AST_$FAILED_FLAGS = flags;

    /* Return "file object not found" status (0x000F0001) */
    return status_$file_object_not_found;
}
