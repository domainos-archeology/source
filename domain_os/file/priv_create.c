/*
 * FILE_$PRIV_CREATE - Create a file (internal/privileged)
 *
 * Original address: 0x00E5D382
 *
 * This is the core file creation function used by all FILE_$CREATE variants.
 * It handles both local and remote file creation, generating UIDs, setting
 * up file attributes, and allocating VTOC entries.
 *
 * Parameters:
 *   file_type    - Type of file to create:
 *                  0 = default file
 *                  1 = directory
 *                  2 = symbolic link
 *                  3 = special (like directory but different ACL)
 *                  4, 5 = invalid for pre-SR10 remote nodes
 *   type_uid     - UID for typed objects (or UID_$NIL for untyped)
 *   dir_uid      - Directory to create file in (UID_$NIL uses NAME_$NODE_UID)
 *   file_uid_ret - Receives the new file's UID
 *   initial_size - Initial file size (0 for default from DAT_00e823e8)
 *   flags        - Creation flags:
 *                  bit 0: force directory creation even if parent is file
 *                  bit 1: use owner_info parameter, skip ACL_$GET_RE_ALL_SIDS
 *   owner_info   - Owner/ACL info (or NULL to get from current process)
 *   status_ret   - Receives operation status
 *
 * Returns:
 *   Byte indicating whether parent was a directory (inverted is_file flag)
 */

#include "file/file_internal.h"
#include "name/name.h"
#include "acl/acl.h"
#include "rem_file/rem_file.h"

/*
 * External references for file creation
 */

/* Default initial file size - at FILE_$LOCK_CONTROL + 0x2C0 */
extern uint32_t FILE_$DEFAULT_SIZE;     /* 0xE823E8 */

/*
 * Nil UIDs for default ownership (owner, group, org)
 * These are copied as a group (24 bytes) when no SID is available.
 */
extern uid_t PPO_$NIL_USER_UID;         /* 0xE174EC: Nil user (owner) UID */
extern uid_t RGYC_$G_NIL_UID;           /* 0xE17524: Nil group UID */
extern uid_t PPO_$NIL_ORG_UID;          /* 0xE17574: Nil org UID */

/*
 * Status codes used
 */
#define status_$vtoc_duplicate_uid                      0x00020007
#define file_$bad_reply_received_from_remote_node 0x000F0003
#define file_$cannot_create_on_remote_with_uid   0x000F000B
#define file_$volume_is_read_only                0x000E0030
#define file_$invalid_type                       0x000F0016

/*
 * File attribute structure for new file creation (144 bytes = 0x90)
 * This matches the layout expected by VTOC_$ALLOCATE
 */
typedef struct file_create_attrs_t {
    uint8_t     flags1;             /* 0x00: Flags byte 1 */
    uint8_t     file_type;          /* 0x01: Object type */
    uint8_t     flags2;             /* 0x02: Flags byte 2 (bit 4 = directory) */
    uint8_t     flags3;             /* 0x03: Flags byte 3 */
    uint32_t    dtc_high;           /* 0x04: Date/time created high */
    uint16_t    dtc_low;            /* 0x08: Date/time created low */
    uint16_t    pad_0a;             /* 0x0A: Padding */
    uid_t       file_uid;           /* 0x0C: File UID */
    uid_t       type_uid;           /* 0x14: Type UID */
    uint32_t    dtm_high;           /* 0x1C: Date/time modified high */
    uint16_t    dtm_low;            /* 0x20: Date/time modified low */
    uint16_t    pad_22;             /* 0x22: Padding */
    uint32_t    dtu_high;           /* 0x24: Date/time used high */
    uint16_t    dtu_low;            /* 0x28: Date/time used low */
    uint32_t    dta_high;           /* 0x2A: Date/time accessed high */
    uint16_t    dta_low;            /* 0x2E: Date/time accessed low */
    uint32_t    dtb_high;           /* 0x30: Date/time backup high */
    uint16_t    dtb_low;            /* 0x34: Date/time backup low */
    uint16_t    pad_36;             /* 0x36: Padding */
    uid_t       parent_uid;         /* 0x38: Parent directory UID */
    uint32_t    refcount;           /* 0x40: Reference count (always 1) */
    uint8_t     acl_data[24];       /* 0x44: ACL owner/group/org UIDs */
    uint32_t    initial_size;       /* 0x5C: Initial file size */
    uint8_t     acl_ext[12];        /* 0x60: Extended ACL data */
    int16_t     is_dir;             /* 0x6C: Is directory flag */
    uint8_t     pad_6e[6];          /* 0x6E: Padding */
    uid_t       default_acl;        /* 0x74: Default ACL UID */
    uint8_t     pad_7c[4];          /* 0x7C: Padding */
    uid_t       vol_uid;            /* 0x80: Volume UID */
    uint8_t     vol_flags;          /* 0x88: Volume flags */
    uint8_t     pad_89[7];          /* 0x89: Padding to 0x90 */
} file_create_attrs_t;

/*
 * Location info structure for parent directory (32 bytes)
 */
typedef struct parent_location_t {
    uid_t       parent_uid;         /* 0x00: Parent directory UID */
    uint32_t    pad_08;             /* 0x08: Padding */
    uint8_t     remote_flag;        /* 0x0C: Remote flag (bit 7 = remote) */
    uint8_t     pad_0d[3];          /* 0x0D: Padding */
    uint8_t     vol_flags;          /* 0x10: Volume flags (bit 1 = read-only) */
    uint8_t     pad_11[15];         /* 0x11: Padding to 0x20 */
} parent_location_t;

uint32_t FILE_$PRIV_CREATE(int16_t file_type, const uid_t *type_uid, uid_t *dir_uid,
                           uid_t *file_uid_ret, uint32_t initial_size,
                           uint16_t flags, uid_t *owner_info, status_$t *status_ret)
{
    status_$t status;
    uint32_t size;
    int16_t is_dir;
    int8_t is_file;
    uid_t parent_uid;

    /*
     * Owner info buffer (48 bytes total):
     *   - Bytes 0x00-0x07: Owner UID
     *   - Bytes 0x08-0x0F: Group UID
     *   - Bytes 0x10-0x17: Org UID
     *   - Bytes 0x18-0x23: Reserved (padding, filled by acl_result in stack layout)
     *   - Bytes 0x24-0x2F: Extended ACL data
     *
     * When no SID is available (acl_result[0] == 0xC), all three nil UIDs
     * are copied here. When owner_info is provided, it points to this same
     * structure layout.
     *
     * Note: In the original assembly, acl_result is stored in the 'reserved'
     * area due to stack layout. When copying to create_attrs, the code reads
     * from owner_ptr + 0x24 to get the extended ACL data.
     */
    struct {
        uid_t owner;        /* 0x00: Owner UID */
        uid_t group;        /* 0x08: Group UID */
        uid_t org;          /* 0x10: Org UID */
        int32_t reserved[3];/* 0x18: Reserved/acl_result values */
        uint8_t ext[12];    /* 0x24: Extended ACL data */
    } owner_buf;
    uint8_t *owner_ptr;
    const uid_t *default_acl;
    int32_t acl_result[3];
    uint8_t acl_data[40];
    uint8_t prot_info[16];

    /* Parent location info - from AST_$GET_ATTRIBUTES */
    struct {
        uint8_t     attrs[0x108];       /* File attributes buffer */
        parent_location_t location;     /* Location info at end */
    } parent_info;

    /* File creation attributes */
    file_create_attrs_t create_attrs;

    /* Set up initial size */
    if (initial_size == 0) {
        size = FILE_$DEFAULT_SIZE;
    } else {
        /* Set bit 27 to mark as explicit size */
        size = initial_size | 0x08000000;
    }

    /* Use NAME_$NODE_UID if dir_uid is nil */
    if (dir_uid->high == UID_$NIL.high && dir_uid->low == UID_$NIL.low) {
        dir_uid = &NAME_$NODE_UID;
    }

    /* Copy parent UID */
    parent_uid.high = dir_uid->high;
    parent_uid.low = dir_uid->low;

    /* Clear remote flag in location */
    parent_info.location.remote_flag &= ~0x40;

    /* Get parent directory attributes to check if remote and get vol info */
    AST_$GET_ATTRIBUTES((uid_t *)&parent_info.location.parent_uid, 0,
                        &parent_info.attrs, &status);

    if (status != status_$ok) {
        if (status != file_$object_not_found) {
            status |= 0x80000000;  /* Mark as severe */
        }
        *status_ret = status;
        return status;
    }

    /* Determine if we're creating a directory-like object */
    is_file = (parent_info.attrs[0] != 0) ? -1 : 0;  /* -1 if parent is file */

    if (file_type == 1) {
        /* Always create directory for type 1 */
        is_dir = 1;
    } else if (((flags & 1) && !is_file) && (file_type != 3)) {
        /* Create directory if flag set and parent is dir, unless type 3 */
        is_dir = 1;
    } else {
        is_dir = 0;
    }

    /* Set up owner pointer - points to 36-byte owner info buffer */
    owner_ptr = (uint8_t *)&owner_buf;

    if ((flags & 2) == 0) {
        /* Get owner/ACL info from current process */
        ACL_$GET_RE_ALL_SIDS(acl_data, &owner_buf.owner, prot_info, acl_result, &status);

        /*
         * If result[0] == 0xC (no SID available), use nil UIDs for
         * owner, group, and org. The assembly shows all three UIDs
         * are copied from separate global addresses.
         */
        if (acl_result[0] == 0x0C) {
            owner_buf.owner.high = PPO_$NIL_USER_UID.high;
            owner_buf.owner.low = PPO_$NIL_USER_UID.low;
            owner_buf.group.high = RGYC_$G_NIL_UID.high;
            owner_buf.group.low = RGYC_$G_NIL_UID.low;
            owner_buf.org.high = PPO_$NIL_ORG_UID.high;
            owner_buf.org.low = PPO_$NIL_ORG_UID.low;
        }

        /*
         * Copy acl_result to owner_buf.ext (at offset 0x24).
         * In the original assembly, acl_result happened to be at
         * owner_ptr + 0x24 due to stack layout. We replicate this
         * by explicitly copying.
         */
        {
            int32_t *dst = (int32_t *)owner_buf.ext;
            dst[0] = acl_result[0];
            dst[1] = acl_result[1];
            dst[2] = acl_result[2];
        }
    } else {
        /* Use provided owner_info or nil UIDs */
        if (owner_info == NULL) {
            /*
             * No owner_info provided - use nil UIDs for all three
             * and mark all SID results as unavailable (0xC).
             */
            owner_buf.owner.high = PPO_$NIL_USER_UID.high;
            owner_buf.owner.low = PPO_$NIL_USER_UID.low;
            owner_buf.group.high = RGYC_$G_NIL_UID.high;
            owner_buf.group.low = RGYC_$G_NIL_UID.low;
            owner_buf.org.high = PPO_$NIL_ORG_UID.high;
            owner_buf.org.low = PPO_$NIL_ORG_UID.low;
            acl_result[0] = 0x0C;
            acl_result[1] = 0x0C;
            acl_result[2] = 0x0C;

            /* Copy to owner_buf.ext for consistency */
            {
                int32_t *dst = (int32_t *)owner_buf.ext;
                dst[0] = 0x0C;
                dst[1] = 0x0C;
                dst[2] = 0x0C;
            }
        } else {
            owner_ptr = (uint8_t *)owner_info;
        }
    }

    /* Check if parent is remote (bit 7 of remote_flag) */
    if ((int8_t)parent_info.location.remote_flag < 0) {
        /* Remote file creation */

        /* Cannot specify owner for remote creation */
        if ((flags & 2) != 0) {
            *status_ret = file_$cannot_create_on_remote_with_uid;
            return 0;
        }

        /* Create remote file */
        REM_FILE_$CREATE_TYPE((uid_t *)&parent_info.location.parent_uid,
                              file_type, (uid_t *)type_uid, size, flags,
                              &owner_buf.owner, parent_info.attrs,
                              &create_attrs.vol_uid, &status);

        /* Copy returned file UID */
        file_uid_ret->high = create_attrs.vol_uid.high;
        file_uid_ret->low = create_attrs.vol_uid.low;

        if (status != status_$ok) {
            if (status == status_$vtoc_duplicate_uid) {
                /* Duplicate UID is OK, clear error */
                status = status_$ok;
            } else if (status == file_$bad_reply_received_from_remote_node) {
                /* Try pre-SR10 protocol for types 4 and 5 */
                if (file_type == 5 || file_type == 4) {
                    status = file_$invalid_arg;
                } else {
                    /* Fallback to pre-SR10 creation */
                    REM_FILE_$CREATE_TYPE_PRESR10((uid_t *)&parent_info.location.parent_uid,
                                                  file_type, is_dir, file_uid_ret, &status);

                    /* If type_uid is not nil and success, set the type attribute */
                    if ((type_uid->high != UID_$NIL.high || type_uid->low != UID_$NIL.low) &&
                        status == status_$ok) {
                        uid_t type_copy;
                        type_copy.high = type_uid->high;
                        type_copy.low = type_uid->low;
                        AST_$SET_ATTRIBUTE(file_uid_ret, 4, &type_copy, &status);
                    }

                    /* If is_dir and success, set directory flag attribute */
                    if (is_dir != 0 && status == status_$ok) {
                        uint8_t dir_flag = 0xFF;
                        AST_$SET_ATTRIBUTE(file_uid_ret, 0, &dir_flag, &status);
                    }
                }
            }
            goto done;
        }
    } else {
        /* Local file creation */

        /* Check for read-only volume (bit 1 of vol_flags) */
        if ((parent_info.location.vol_flags & 0x02) != 0) {
            if (file_type == 1 || file_type == 2) {
                *status_ret = file_$volume_is_read_only;
                return 0;
            }
            *status_ret = file_$invalid_type;
            return 0;
        }

        /* Generate new UID if not using provided owner */
        if ((flags & 2) == 0) {
            UID_$GEN(file_uid_ret);
        }

        /* Clear the attribute structure */
        {
            int16_t i;
            uint32_t *p = (uint32_t *)&create_attrs;
            for (i = 0x23; i >= 0; i--) {
                *p++ = 0;
            }
        }

        /* Set directory flag in flags2 (bit 4) */
        create_attrs.flags2 = (create_attrs.flags2 & ~0x10) |
                              ((is_dir != 0 ? 0x80 : 0x00) >> 3);

        /* Set timestamps to current time */
        create_attrs.dtc_high = TIME_$CURRENT_CLOCKH;
        create_attrs.dtc_low = 0;
        create_attrs.file_type = (uint8_t)file_type;

        /* Set file UID */
        create_attrs.file_uid.high = file_uid_ret->high;
        create_attrs.file_uid.low = file_uid_ret->low;

        /* Set type UID */
        create_attrs.type_uid.high = type_uid->high;
        create_attrs.type_uid.low = type_uid->low;

        /* Set modification time */
        create_attrs.dtm_high = TIME_$CURRENT_CLOCKH;
        create_attrs.dtm_low = 0;

        /* Set access time */
        create_attrs.dtu_high = TIME_$CURRENT_CLOCKH;
        create_attrs.dtu_low = 0;

        /* Set backup time (use TIME_$CLOCKH for adjusted clock) */
        create_attrs.dta_high = TIME_$CLOCKH;

        /* Set another timestamp */
        create_attrs.dtb_high = TIME_$CURRENT_CLOCKH;
        create_attrs.dtb_low = 0;

        /* Set parent directory UID */
        create_attrs.parent_uid.high = parent_uid.high;
        create_attrs.parent_uid.low = parent_uid.low;

        /* Set reference count to 1 */
        create_attrs.refcount = 1;

        /* Copy ACL data (24 bytes from owner_ptr) */
        {
            int16_t i;
            uint8_t *src = (uint8_t *)owner_ptr;
            uint8_t *dst = create_attrs.acl_data;
            for (i = 0x17; i >= 0; i--) {
                *dst++ = *src++;
            }
        }

        /* Set initial size */
        create_attrs.initial_size = size;

        /* Copy extended ACL data (12 bytes from owner_ptr + 0x24) */
        {
            uint32_t *src = (uint32_t *)((uint8_t *)owner_ptr + 0x24);
            uint32_t *dst = (uint32_t *)create_attrs.acl_ext;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
        }

        /* Set is_dir flag */
        create_attrs.is_dir = is_dir;

        /* Select default ACL based on object type and whether parent is file */
        if (is_file) {
            default_acl = &UID_$NIL;
        } else if (file_type == 1 || file_type == 2) {
            /* Directory or link - use DNDCAL */
            default_acl = &ACL_$DNDCAL;
        } else if (file_type == 3) {
            default_acl = &UID_$NIL;
        } else {
            /* Regular file - use FNDWRX */
            default_acl = &ACL_$FNDWRX;
        }

        create_attrs.default_acl.high = default_acl->high;
        create_attrs.default_acl.low = default_acl->low;

        /* Copy file UID to vol_uid location for VTOC */
        create_attrs.vol_uid.high = file_uid_ret->high;
        create_attrs.vol_uid.low = file_uid_ret->low;

        /* Copy volume flag from parent location */
        create_attrs.vol_flags = parent_info.location.remote_flag;

        /* Copy parent volume reference */
        /* create_attrs at offset 0x80 gets parent_info.location at 0x74 */

        /* Allocate VTOC entry for the new file */
        VTOC_$ALLOCATE(&create_attrs.vol_uid, parent_info.attrs, &status);

        if (status != status_$ok) {
            goto done;
        }
    }

    /* Load the new file's AOTE into the active object table */
    AST_$LOAD_AOTE((uint32_t *)parent_info.attrs, (uint32_t *)&create_attrs.vol_uid);

done:
    *status_ret = status;
    return (uint32_t)(uint8_t)(~is_file);
}
