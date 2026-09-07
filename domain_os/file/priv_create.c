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

/*
 * Nil UIDs for default ownership (owner, group, org)
 * These are copied as a group (24 bytes) when no SID is available.
 * PPO_$NIL_USER_UID / PPO_$NIL_ORG_UID come from vtoc/vtoc.h,
 * RGYC_$G_NIL_UID from rgyc/rgyc.h.
 */
#include "rgyc/rgyc.h"
#include "vtoc/vtoc.h"      /* vtoc_$lookup_req_t, VTOC_$ALLOCATE */

/*
 * Status codes used
 */
#define status_$vtoc_duplicate_uid                      0x00020007
#define file_$bad_reply_received_from_remote_node 0x000F0003
#define file_$cannot_create_on_remote_with_uid   0x000F000B
#define file_$volume_is_read_only                0x000E0030
#define file_$invalid_type                       0x000F0016

/*
 * The new-format VTOCE image the local-create path builds (0x90 bytes,
 * A6-0x108).  Field offsets recovered store by store from 0x00E5D63E
 * onwards; everything not listed is left zero by the 36-longword clear at
 * 0x00E5D640 (`moveq #0x23,D0` + `dbf`).
 *
 * NOTE: this is the SAME 0x90-byte stack buffer AST_$GET_ATTRIBUTES filled
 * earlier in the function (`pea (-0x108,A6)` at 0x00E5D3F2) - see obj_buf
 * below.
 */
typedef struct file_create_attrs_t {
    uint8_t     flags1;             /* 0x00: attrs[0] - parent "is a file" byte */
    uint8_t     file_type;          /* 0x01: 0x00E5D66C */
    uint8_t     flags2;             /* 0x02: bit 4 = directory (0x00E5D654) */
    uint8_t     flags3;             /* 0x03: attrs[3] - bit 1 = read-only volume */
    uid_t       file_uid;           /* 0x04: 0x00E5D672 */
    uid_t       type_uid;           /* 0x0C: 0x00E5D67C */
    uint32_t    reserved_14[2];     /* 0x14: never written */
    uint32_t    dtm_high;           /* 0x1C: 0x00E5D684 */
    uint16_t    dtm_low;            /* 0x20: 0x00E5D68A */
    uint16_t    pad_22;             /* 0x22 */
    uint32_t    dtu_high;           /* 0x24: 0x00E5D690 */
    uint16_t    dtu_low;            /* 0x28: 0x00E5D696 */
    uint16_t    pad_2a;             /* 0x2A */
    uint32_t    dta_high;           /* 0x2C: TIME_$CLOCKH (0x00E5D69C) */
    uint32_t    reserved_30;        /* 0x30: never written */
    uint32_t    dtb_high;           /* 0x34: 0x00E5D6A4 */
    uint16_t    dtb_low;            /* 0x38: 0x00E5D6AA */
    uint16_t    pad_3a;             /* 0x3A */
    uid_t       parent_uid;         /* 0x3C: 0x00E5D6B4, from the location record */
    uint32_t    refcount;           /* 0x44: always 1 (0x00E5D6BE) */
    uint8_t     acl_data[24];       /* 0x48: 24 bytes from owner_ptr (0x00E5D6CC) */
    uint32_t    initial_size;       /* 0x60: 0x00E5D6D2 */
    uint32_t    reserved_64;        /* 0x64: never written */
    uint8_t     acl_ext[12];        /* 0x68: 12 bytes from owner_ptr+0x24 */
    int16_t     is_dir;             /* 0x74: 0x00E5D6EA */
    uint8_t     reserved_76[18];    /* 0x76: never written */
    uid_t       default_acl;        /* 0x88: 0x00E5D71C */
} file_create_attrs_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(file_create_attrs_t, file_uid)     == 0x04, "vtoce.file_uid");
_Static_assert(offsetof(file_create_attrs_t, type_uid)     == 0x0C, "vtoce.type_uid");
_Static_assert(offsetof(file_create_attrs_t, dtm_high)     == 0x1C, "vtoce.dtm");
_Static_assert(offsetof(file_create_attrs_t, dtu_high)     == 0x24, "vtoce.dtu");
_Static_assert(offsetof(file_create_attrs_t, dta_high)     == 0x2C, "vtoce.dta");
_Static_assert(offsetof(file_create_attrs_t, dtb_high)     == 0x34, "vtoce.dtb");
_Static_assert(offsetof(file_create_attrs_t, parent_uid)   == 0x3C, "vtoce.parent_uid");
_Static_assert(offsetof(file_create_attrs_t, refcount)     == 0x44, "vtoce.refcount");
_Static_assert(offsetof(file_create_attrs_t, acl_data)     == 0x48, "vtoce.acl_data");
_Static_assert(offsetof(file_create_attrs_t, initial_size) == 0x60, "vtoce.size");
_Static_assert(offsetof(file_create_attrs_t, acl_ext)      == 0x68, "vtoce.acl_ext");
_Static_assert(offsetof(file_create_attrs_t, is_dir)       == 0x74, "vtoce.is_dir");
_Static_assert(offsetof(file_create_attrs_t, default_acl)  == 0x88, "vtoce.default_acl");
_Static_assert(sizeof(file_create_attrs_t) == AST_ATTR_REC_SIZE, "sizeof vtoce");
#endif

uint32_t FILE_$PRIV_CREATE(int16_t file_type, const uid_t *type_uid, uid_t *dir_uid,
                           uid_t *file_uid_ret, uint32_t initial_size,
                           uint16_t flags, uid_t *owner_info, status_$t *status_ret)
{
    status_$t status;
    uint32_t size;
    int16_t is_dir;
    int8_t is_file;
    uid_t parent_uid;
    clock_t current_clock;          /* A6-0x17C: TIME_$CURRENT_CLOCKH, low = 0 */

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

    /*
     * A6-0x108: ONE 0x90-byte buffer with two lives.  AST_$GET_ATTRIBUTES
     * fills it with the parent's attribute record; the local-create path
     * then clears it (0x00E5D640) and rebuilds it as the new object's
     * VTOCE image before handing the same address to VTOC_$ALLOCATE
     * (`pea (-0x108,A6)` at both 0x00E5D3F2 and 0x00E5D73E).
     */
    union {
        uint8_t             attrs[AST_ATTR_REC_SIZE];
        file_create_attrs_t vtoce;
    } obj_buf;

    /* A6-0x78: the 0x20-byte parent object-location record */
    file_$obj_loc_t parent_loc;

    /*
     * A6-0x58: the VTOC location descriptor.  Same 0x20-byte shape as
     * parent_loc; VTOC_$ALLOCATE rewrites it on every exit path, and
     * REM_FILE_$CREATE_TYPE returns the new object's UID in its +0x08 field.
     */
    vtoc_$lookup_req_t vtoc_loc;

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

    /*
     * Seed the location record with the parent UID at +0x08 and clear bit 6
     * of its flags byte at +0x1D (0x00E5D3DE / 0x00E5D3E6).
     */
    parent_loc.uid = parent_uid;
    parent_loc.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

    /* Get parent directory attributes to check if remote and get vol info */
    AST_$GET_ATTRIBUTES(&parent_loc, 0, obj_buf.attrs, &status);

    if (status != status_$ok) {
        if (status != file_$object_not_found) {
            status |= 0x80000000;  /* Mark as severe */
        }
        *status_ret = status;
        return status;
    }

    /* Determine if we're creating a directory-like object */
    is_file = (obj_buf.attrs[0] != 0) ? -1 : 0;  /* -1 if parent is file */

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
    if (parent_loc.flags < 0) {
        /* Remote file creation */

        /* Cannot specify owner for remote creation */
        if ((flags & 2) != 0) {
            *status_ret = file_$cannot_create_on_remote_with_uid;
            return 0;
        }

        /* Create remote file */
        REM_FILE_$CREATE_TYPE((uid_t *)&parent_loc, file_type,
                              (uid_t *)type_uid, size, flags,
                              &owner_buf.owner, obj_buf.attrs,
                              &vtoc_loc.uid, &status);

        /* 0x00E5D53C: the new UID comes back at the record's +0x08 */
        file_uid_ret->high = vtoc_loc.uid.high;
        file_uid_ret->low = vtoc_loc.uid.low;

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
                    REM_FILE_$CREATE_TYPE_PRESR10((uid_t *)&parent_loc,
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

        /* 0x00E5D602 `btst.b #0x1,(-0x105,A6)`: bit 1 of the ATTRIBUTE
         * record's byte 3, not of the location record. */
        if ((obj_buf.attrs[3] & 0x02) != 0) {
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

        /* 0x00E5D640: clear all 36 longwords of the 0x90-byte buffer */
        {
            int16_t i;
            uint32_t *p = (uint32_t *)(void *)&obj_buf;
            for (i = 0x23; i >= 0; i--) {
                *p++ = 0;
            }
        }

        /* Set directory flag in flags2 (bit 4), 0x00E5D654 */
        obj_buf.vtoce.flags2 = (uint8_t)((obj_buf.vtoce.flags2 & ~0x10) |
                                         ((is_dir != 0 ? 0x80 : 0x00) >> 3));

        /*
         * A6-0x17C/-0x178: the clock_t temporary the three date fields are
         * copied from (0x00E5D660 / 0x00E5D668).
         */
        current_clock.high = TIME_$CURRENT_CLOCKH;
        current_clock.low = 0;

        obj_buf.vtoce.file_type = (uint8_t)file_type;    /* 0x01 */

        /* 0x04: the new object UID */
        obj_buf.vtoce.file_uid.high = file_uid_ret->high;
        obj_buf.vtoce.file_uid.low = file_uid_ret->low;

        /* 0x0C: the type UID */
        obj_buf.vtoce.type_uid.high = type_uid->high;
        obj_buf.vtoce.type_uid.low = type_uid->low;

        /* 0x1C / 0x24 / 0x34: three copies of the same clock temporary */
        obj_buf.vtoce.dtm_high = current_clock.high;
        obj_buf.vtoce.dtm_low = current_clock.low;
        obj_buf.vtoce.dtu_high = current_clock.high;
        obj_buf.vtoce.dtu_low = current_clock.low;

        /* 0x2C: TIME_$CLOCKH only - no low word is stored */
        obj_buf.vtoce.dta_high = TIME_$CLOCKH;

        obj_buf.vtoce.dtb_high = current_clock.high;
        obj_buf.vtoce.dtb_low = current_clock.low;

        /*
         * 0x3C: 0x00E5D6B0 reads A6-0x70, i.e. the location record's UID
         * field - the copy AST_$GET_ATTRIBUTES wrote back, not the caller's
         * original `parent_uid`.
         */
        obj_buf.vtoce.parent_uid.high = parent_loc.uid.high;
        obj_buf.vtoce.parent_uid.low = parent_loc.uid.low;

        /* 0x44: reference count of 1 */
        obj_buf.vtoce.refcount = 1;

        /* 0x48: 24 bytes from owner_ptr (0x00E5D6CC, `moveq #0x17` + dbf) */
        {
            int16_t i;
            uint8_t *src = (uint8_t *)owner_ptr;
            uint8_t *dst = obj_buf.vtoce.acl_data;
            for (i = 0x17; i >= 0; i--) {
                *dst++ = *src++;
            }
        }

        /* 0x60: initial size */
        obj_buf.vtoce.initial_size = size;

        /* 0x68: three longwords from owner_ptr+0x24 (0x00E5D6E4) */
        {
            uint32_t *src = (uint32_t *)(void *)((uint8_t *)owner_ptr + 0x24);
            uint32_t *dst = (uint32_t *)(void *)obj_buf.vtoce.acl_ext;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
        }

        /* 0x74: is-a-directory word */
        obj_buf.vtoce.is_dir = is_dir;

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

        /* 0x88: default ACL UID (0x00E5D71C) */
        obj_buf.vtoce.default_acl.high = default_acl->high;
        obj_buf.vtoce.default_acl.low = default_acl->low;

        /*
         * Fill in the VTOC location descriptor: the new object's UID at
         * +0x08 (0x00E5D726), the parent record's byte at +0x1C
         * (0x00E5D72E) and its longword at +0x04 (0x00E5D734).
         */
        vtoc_loc.uid.high = file_uid_ret->high;
        vtoc_loc.uid.low = file_uid_ret->low;
        vtoc_loc.vol_idx = (uint8_t)parent_loc.rights_bits;
        vtoc_loc.block_hint = parent_loc.reserved_00[1];

        /* Allocate VTOC entry for the new file */
        VTOC_$ALLOCATE(&vtoc_loc, &obj_buf.vtoce, &status);

        if (status != status_$ok) {
            goto done;
        }
    }

    /* Load the new file's AOTE into the active object table */
    /* 0x00E5D756: the attribute buffer and the location descriptor, both
     * passed by address (`pea (-0x108,A6)` / `pea (-0x58,A6)`). */
    AST_$LOAD_AOTE((uint32_t *)(void *)obj_buf.attrs,
                   (uint32_t *)(void *)&vtoc_loc);

done:
    *status_ret = status;
    return (uint32_t)(uint8_t)(~is_file);
}
