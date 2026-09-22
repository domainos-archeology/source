/*
 * ast_$validate_uid - Record a failed object lookup and return "not found"
 *
 * Stores the UID and the caller's flags word in the AST_ block for
 * post-mortem inspection and returns file_$object_not_found.  Despite
 * the name it validates nothing.
 *
 * Original address: 0x00E00BE8 (32 bytes).  A5 is inherited (every caller
 * is AST or VTOC_$SEARCH_VOLUMES code with A5 = 0xE1DC80): (0x478,A5)
 * and (0x47C,A5) take the UID, (0x480,A5) the flags - the record the SAU2
 * link map names AST_$NOT_FOUND (0xE1E0F8).
 * Frame: (0x8,A6) uid, (0xC,A6) flags longword.
 */

#include "ast/ast_internal.h"

status_$t ast_$validate_uid(uid_t *uid, uint32_t flags)
{
    /* 0x00E00BEC..0x00E00BF8: two post-increment longwords, then the flags */
    AST_$NOT_FOUND.uid.high = uid->high;
    AST_$NOT_FOUND.uid.low = uid->low;
    AST_$NOT_FOUND.flags = flags;

    /* 0x00E00BFE: move.l #0xf0001,D0 */
    return file_$object_not_found;
}
