/*
 * AST_$SET_ATTR_DISPATCH - Attribute setting dispatch function
 *
 * Large switch statement (27+ cases) that dispatches attribute setting
 * by operation code. Handles timestamps, UIDs, flags, counts, access modes.
 *
 * This was originally a nested Pascal procedure that accessed the caller's
 * frame directly. For portability to non-M68K architectures, this version
 * takes explicit parameters.
 *
 * Parameters:
 *   aote       - AOTE being modified
 *   attr_type  - Attribute type code (0-27)
 *   value      - Pointer to attribute value
 *   wait_flag  - Wait flag
 *   clock_info - Clock info for timestamps
 *   status     - Output status code
 *
 * Original address: 0x00E04B00
 * Original size: 1756 bytes
 */

#include "ast/ast_internal.h"

/* Status codes */
#define status_$ast_invalid_attribute_type     0x00030006
#define status_$ast_object_special_attribute   0x000F0016
#define status_$ast_refcount_underflow         0x00030007

/* Attribute type constants */
#define ATTR_TYPE_READONLY       0    /* Read-only flag */
#define ATTR_TYPE_COPY_ON_WRITE  1    /* Copy-on-write flag */
#define ATTR_TYPE_DIRTY          2    /* Dirty flag */
#define ATTR_TYPE_ACL_UID        3    /* ACL UID */
#define ATTR_TYPE_CREATION_TIME  4    /* Creation timestamp */
#define ATTR_TYPE_MOD_TIME       5    /* Modification timestamp */
#define ATTR_TYPE_ADD_REFCOUNT   6    /* Increment reference count */
#define ATTR_TYPE_SUB_REFCOUNT   7    /* Decrement reference count */
#define ATTR_TYPE_SET_REFCOUNT   8    /* Set reference count */
#define ATTR_TYPE_SIZE           9    /* File size */
#define ATTR_TYPE_DTM            10   /* Data timestamp */
#define ATTR_TYPE_BLOCKS         11   /* Block count */
#define ATTR_TYPE_ACCESS_FLAG    12   /* Access flags */
#define ATTR_TYPE_ACCESS_MODE    13   /* Access mode bits */
#define ATTR_TYPE_OWNER1_UID     14   /* Owner 1 UID */
#define ATTR_TYPE_OWNER2_UID     15   /* Owner 2 UID */
#define ATTR_TYPE_SET_OWNER1     16   /* Set owner 1 with timestamp */
#define ATTR_TYPE_SET_OWNER2     17   /* Set owner 2 with timestamp */
#define ATTR_TYPE_SET_OWNER3     18   /* Set owner 3 with timestamp */
#define ATTR_TYPE_SET_ALL_OWNERS 19   /* Set all owners */
#define ATTR_TYPE_SET_ALL_EXT    20   /* Set all extended attributes */
#define ATTR_TYPE_SET_MODES      21   /* Set mode flags */
#define ATTR_TYPE_SET_LINKCOUNT  22   /* Set link count */
#define ATTR_TYPE_SIZE_AND_DTM   23   /* Size with DTM */
#define ATTR_TYPE_SIZE_AND_DTM2  24   /* Size with DTM variant */
#define ATTR_TYPE_SPECIAL_FLAG   25   /* Special attribute flag */
#define ATTR_TYPE_UPDATE_DTM     26   /* Update DTM from current */
#define ATTR_TYPE_UPDATE_DTM2    27   /* Update DTM from current variant */

/* Global - attribute timestamp mask at A5+0x48C */
#if defined(M68K)
#include "arch/m68k/arch.h"
#define AST_$ATTR_TIMESTAMP_MASK (*(uint32_t *)((char *)__A5_BASE() + 0x48C))
#else
extern uint32_t ast_$attr_timestamp_mask;
#define AST_$ATTR_TIMESTAMP_MASK ast_$attr_timestamp_mask
#endif

void AST_$SET_ATTR_DISPATCH(aote_t *aote, uint16_t attr_type, void *value,
                            int8_t wait_flag, clock_t *clock_info, status_$t *status)
{
    int8_t needs_purify = 0;
    int8_t needs_truncate = 0;
    uint8_t obj_type;
    uid_t old_acl_uid;
    uid_t new_acl_uid;

    *status = status_$ok;

    /* Lock attribute operations */
    ML_$LOCK(0x14);

    /* Get object type from AOTE (offset 0x0C) */
    obj_type = *((uint8_t *)((char *)aote + 0x0C));

    /*
     * Check if attribute type is valid for this object
     * Valid types are bits 0-13 (0x3FFF mask)
     */
    if (obj_type == 0 && (((uint32_t)1 << attr_type) & 0x3FFF) == 0) {
        *status = status_$ast_invalid_attribute_type;
        goto unlock_and_return;
    }

    /*
     * Check for special object (bit 1 at offset 0x0F)
     * Special objects only allow attr types 5 (mod_time) and 11 (blocks)
     */
    if ((*((uint8_t *)((char *)aote + 0x0F)) & 2) != 0) {
        if (attr_type == ATTR_TYPE_MOD_TIME) {
            /* Set modification time */
            uint32_t *time_ptr = (uint32_t *)value;
            *((uint32_t *)((char *)aote + 0x48)) = time_ptr[0];
            *((uint32_t *)((char *)aote + 0x4C)) = time_ptr[1];
        } else if (attr_type == ATTR_TYPE_BLOCKS) {
            /* Set block count */
            *((uint32_t *)((char *)aote + 0x50)) = *(uint32_t *)value;
        } else {
            *status = status_$ast_object_special_attribute;
        }
        goto unlock_and_return;
    }

    /*
     * Main dispatch switch based on attribute type
     */
    switch (attr_type) {
    case ATTR_TYPE_READONLY:
        /* Set read-only flag (bit 4 at offset 0x0E) */
        {
            uint8_t flag = *(uint8_t *)value;
            *((uint8_t *)((char *)aote + 0x0E)) &= 0xEF;  /* Clear bit 4 */
            *((uint8_t *)((char *)aote + 0x0E)) |= (flag >> 7) << 4;
        }
        break;

    case ATTR_TYPE_COPY_ON_WRITE:
        /* Set copy-on-write flag (bit 3 at offset 0x0E) */
        {
            uint8_t flag = *(uint8_t *)value;
            *((uint8_t *)((char *)aote + 0x0E)) &= 0xF7;  /* Clear bit 3 */
            *((uint8_t *)((char *)aote + 0x0E)) |= (flag >> 7) << 3;
        }
        break;

    case ATTR_TYPE_DIRTY:
        /* Set dirty flag (bit 2 at offset 0x0E) */
        {
            uint8_t flag = *(uint8_t *)value;
            *((uint8_t *)((char *)aote + 0x0E)) &= 0xFB;  /* Clear bit 2 */
            *((uint8_t *)((char *)aote + 0x0E)) |= (flag >> 7) << 2;
        }
        break;

    case ATTR_TYPE_ACL_UID:
        /* Set ACL UID (offset 0x94) */
        {
            uint32_t *uid_ptr = (uint32_t *)value;
            /* Check if UID is changing */
            if (*((uint32_t *)((char *)aote + 0x94)) == uid_ptr[0] &&
                *((uint32_t *)((char *)aote + 0x98)) == uid_ptr[1]) {
                goto unlock_and_return;  /* No change */
            }
            /* Save old UID for potential truncation */
            old_acl_uid.high = *((uint32_t *)((char *)aote + 0x94));
            old_acl_uid.low = *((uint32_t *)((char *)aote + 0x98));
            new_acl_uid.high = uid_ptr[0];
            new_acl_uid.low = uid_ptr[1];
            needs_purify = -1;
            /* Set new ACL UID */
            *((uint32_t *)((char *)aote + 0x94)) = uid_ptr[0];
            *((uint32_t *)((char *)aote + 0x98)) = uid_ptr[1];
            /* Reset access mode bits */
            *((uint8_t *)((char *)aote + 0x6C)) = 0x10;
            *((uint8_t *)((char *)aote + 0x6D)) = 0x10;
            *((uint8_t *)((char *)aote + 0x6E)) = 0x10;
            *((uint8_t *)((char *)aote + 0x6F)) = 0;
            *((uint8_t *)((char *)aote + 0x70)) = 0;
        }
        break;

    case ATTR_TYPE_CREATION_TIME:
        /* Set creation time (offset 0x18) */
        {
            uint32_t *time_ptr = (uint32_t *)value;
            *((uint32_t *)((char *)aote + 0x18)) = time_ptr[0];
            *((uint32_t *)((char *)aote + 0x1C)) = time_ptr[1];
        }
        break;

    case ATTR_TYPE_MOD_TIME:
        /* Set modification time (offset 0x48) */
        {
            uint32_t *time_ptr = (uint32_t *)value;
            *((uint32_t *)((char *)aote + 0x48)) = time_ptr[0];
            *((uint32_t *)((char *)aote + 0x4C)) = time_ptr[1];
        }
        break;

    case ATTR_TYPE_ADD_REFCOUNT:
        /* Increment reference count (offset 0x80) */
        {
            uint16_t refcount = *((uint16_t *)((char *)aote + 0x80));
            if (refcount > 0xFFF4) {
                goto unlock_and_return;  /* Overflow protection */
            }
            *((uint16_t *)((char *)aote + 0x80)) = refcount + 1;
            *((uint8_t *)((char *)aote + 0x0E)) |= 0x10;  /* Set modified flag */
        }
        break;

    case ATTR_TYPE_SUB_REFCOUNT:
        /* Decrement reference count */
        {
            uint16_t refcount = *((uint16_t *)((char *)aote + 0x80));
            uint8_t obj_type_val = *((uint8_t *)((char *)aote + 0x0D));
            if (refcount > 0xFFF4) {
                goto unlock_and_return;
            }
            if (refcount == 0 || (refcount == 1 && (obj_type_val == 1 || obj_type_val == 2))) {
                *status = status_$ast_refcount_underflow;
                goto unlock_and_return;
            }
            refcount--;
            *((uint16_t *)((char *)aote + 0x80)) = refcount;
            if (refcount == 0) {
                *((uint8_t *)((char *)aote + 0x0E)) &= 0xEF;  /* Clear modified flag */
                *status = status_$ast_refcount_underflow;
            }
        }
        break;

    case ATTR_TYPE_SET_REFCOUNT:
        /* Set reference count directly */
        {
            int16_t new_count = *(int16_t *)value;
            *((uint16_t *)((char *)aote + 0x80)) = new_count;
            *((uint8_t *)((char *)aote + 0x0E)) &= 0xEF;
            if (new_count != 0) {
                *((uint8_t *)((char *)aote + 0x0E)) |= 0x10;
            }
        }
        break;

    case ATTR_TYPE_SIZE:
        /* Set file size (offset 0x28) */
        *((uint32_t *)((char *)aote + 0x28)) = *(uint32_t *)value;
        *((uint16_t *)((char *)aote + 0x2C)) = 0;
        break;

    case ATTR_TYPE_DTM:
        /* Set data timestamp (offset 0x30) */
        *((uint32_t *)((char *)aote + 0x30)) = *(uint32_t *)value;
        *((uint16_t *)((char *)aote + 0x34)) = 0;
        /* Clear timestamp pending flag */
        *((uint8_t *)((char *)aote + 0xBF)) &= 0xEF;
        break;

    case ATTR_TYPE_BLOCKS:
        /* Set block count (offset 0x50) */
        if (*((uint32_t *)((char *)aote + 0x50)) == *(uint32_t *)value) {
            goto unlock_and_return;  /* No change */
        }
        *((uint32_t *)((char *)aote + 0x50)) = *(uint32_t *)value;
        needs_truncate = needs_purify;
        goto set_dirty_flag;

    case ATTR_TYPE_ACCESS_FLAG:
        /* Set access flags (bit 7 at offset 0x71) */
        {
            uint8_t flag = *(uint8_t *)value;
            *((uint8_t *)((char *)aote + 0x71)) &= 0x7F;
            *((uint8_t *)((char *)aote + 0x71)) |= (flag & 0x80);
        }
        break;

    case ATTR_TYPE_ACCESS_MODE:
        /* Set access mode bits at offset 0x71 */
        {
            uint16_t mode = *(uint16_t *)value;
            /* Bit 0 -> bit 5 */
            *((uint8_t *)((char *)aote + 0x71)) &= 0xDF;
            *((uint8_t *)((char *)aote + 0x71)) |= ((mode & 1) ? 0x20 : 0);
            /* Bit 1 -> bit 4 */
            *((uint8_t *)((char *)aote + 0x71)) &= 0xEF;
            *((uint8_t *)((char *)aote + 0x71)) |= ((mode & 2) ? 0x10 : 0);
        }
        break;

    /* Cases 14-27 handle extended attributes - simplified here */
    case ATTR_TYPE_OWNER1_UID:
    case ATTR_TYPE_OWNER2_UID:
    case ATTR_TYPE_SET_OWNER1:
    case ATTR_TYPE_SET_OWNER2:
    case ATTR_TYPE_SET_OWNER3:
    case ATTR_TYPE_SET_ALL_OWNERS:
    case ATTR_TYPE_SET_ALL_EXT:
    case ATTR_TYPE_SET_MODES:
    case ATTR_TYPE_SET_LINKCOUNT:
    case ATTR_TYPE_SIZE_AND_DTM:
    case ATTR_TYPE_SIZE_AND_DTM2:
    case ATTR_TYPE_SPECIAL_FLAG:
    case ATTR_TYPE_UPDATE_DTM:
    case ATTR_TYPE_UPDATE_DTM2:
        /* TODO: Implement extended attribute cases */
        /* These are complex and involve UID copying, timestamp updates, etc. */
        break;

    default:
        *status = status_$ast_invalid_attribute_type;
        goto unlock_and_return;
    }

    /* Update modification time (offset 0x40) */
    *((uint32_t *)((char *)aote + 0x40)) = clock_info->high;
    *((uint16_t *)((char *)aote + 0x44)) = (uint16_t)clock_info->low;

set_dirty_flag:
    /* Set dirty flag at offset 0xBF, bit 5 */
    *((uint8_t *)((char *)aote + 0xBF)) |= 0x20;

    /* Update absolute timestamp if this attribute type requires it */
    if ((AST_$ATTR_TIMESTAMP_MASK & ((uint32_t)1 << attr_type)) != 0) {
        /* Only for local objects (offset 0xB9 >= 0) */
        if (*((int8_t *)((char *)aote + 0xB9)) >= 0) {
            TIME_$ABS_CLOCK((clock_t *)((char *)aote + 0x38));
        }
    }

unlock_and_return:
    ML_$UNLOCK(0x14);

    /* If we need to purify (ACL changed), do so now */
    if (needs_truncate < 0 && *((int8_t *)((char *)aote + 0xB9)) >= 0) {
        ast_$purify_aote(aote, 0xFF, status);
        ML_$UNLOCK(AST_LOCK_ID);

        if (*status == status_$ok) {
            /* If new ACL is non-nil, increment its refcount */
            if (new_acl_uid.high != 0) {
                int32_t one = 1;
                ast_$set_attribute_internal(&new_acl_uid, ATTR_TYPE_ADD_REFCOUNT,
                                           &one, wait_flag, NULL, clock_info, status);
                if (*status != status_$ok) {
                    return;
                }
            }
            /* If old ACL is non-nil, truncate it */
            if (old_acl_uid.high != 0) {
                status_$t truncate_status;
                AST_$TRUNCATE(&old_acl_uid, 0, 3, NULL, &truncate_status);
                /* Ignore "object not found" error */
                if (truncate_status == 0x000F0001) {
                    /* status_$ok already set */
                }
            }
        }
    } else {
        ML_$UNLOCK(AST_LOCK_ID);
    }
}
