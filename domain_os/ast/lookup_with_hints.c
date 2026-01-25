/*
 * AST_$LOOKUP_WITH_HINTS - Higher-level object lookup using hints
 *
 * Looks up an object UID using hint information for efficient network
 * resolution. Tries local node first (NODE_$ME), then falls back to
 * network nodes via NETWORK_$AST_GET_INFO.
 *
 * Parameters:
 *   uid_info   - UID info structure (vtoc_$lookup_req_t format at offset 8)
 *   vol_ptr    - Output: volume/network info
 *   attrs      - Output: object attributes
 *   status     - Output: status code
 *
 * Original address: 0x00E01C52
 * Original size: 274 bytes
 */

#include "ast/ast_internal.h"
#include "hint/hint.h"

/* Net info flags structure for NETWORK_$AST_GET_INFO */
#define NET_INFO_FLAGS ((void *)0xE01D64)

/* Status codes */
#define file_$object_not_found 0x000F0001

void AST_$LOOKUP_WITH_HINTS(void *uid_info, uint32_t *vol_ptr, void *attrs,
                            status_$t *status)
{
    int8_t tried_local = 0;
    int16_t hint_count;
    int16_t i;
    uint32_t hints[14];  /* Hint array - pairs of (network, node) */

    /* Check if target is local node */
    uint32_t target_node = (*(uint32_t *)((char *)uid_info + 0x0C)) & 0xFFFFF;
    if (target_node == NODE_$ME) {
        VTOC_$SEARCH_VOLUMES(uid_info, status);
        if (*status == status_$ok) {
            return;
        }
        tried_local = -1;
    }

    /* Get hints for this UID */
    hint_count = HINT_$GET_HINTS((uid_t *)((char *)uid_info + 8), &hints[2]);
    hint_count--;

    if (hint_count >= 0) {
        for (i = 1; hint_count >= 0; hint_count--, i++) {
            uint32_t hint_node = hints[i * 2 + 1];

            if (hint_node == NODE_$ME) {
                /* Hint points to local node */
                if (tried_local >= 0) {
                    VTOC_$SEARCH_VOLUMES(uid_info, status);
                    if (*status == status_$ok) {
                        return;
                    }
                    tried_local = -1;
                }
            } else {
                /* Try network node */
                /* Update UID info with hint network/node */
                *(uint32_t *)((char *)uid_info + 0x10) = hints[i * 2];
                *(uint32_t *)((char *)uid_info + 0x14) = hint_node;

                /* Clear bit 6 at offset 0x1D (cache flag?) */
                *(uint8_t *)((char *)uid_info + 0x1D) &= 0xBF;

                NETWORK_$AST_GET_INFO(uid_info, NET_INFO_FLAGS, attrs, status);

                if (*status == status_$ok) {
                    /* Success - update volume info with network */
                    *(uint8_t *)vol_ptr |= 0x80;  /* Set remote flag */
                    NETWORK_$INSTALL_NET(hints[i * 2], vol_ptr, status);
                    *vol_ptr = (*vol_ptr & 0xFFF00000) | hints[i * 2 + 1];
                    return;
                }

                /* If target matches hint node, validate UID */
                if (target_node == hint_node) {
                    ast_$validate_uid((uid_t *)((char *)uid_info + 8), *status);
                }
            }
        }
    }

    /* Not found anywhere */
    *status = file_$object_not_found;
}
