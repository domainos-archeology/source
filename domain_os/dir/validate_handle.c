/*
 * DIR_$VALIDATE_HANDLE - Validate directory handle
 *
 * Validates that an opened directory handle is consistent and maps
 * the directory pages into memory. Performs several checks:
 *
 * 1. Gets common attributes via AST_$GET_COMMON_ATTRIBUTES
 * 2. Checks for remote/deleted objects
 * 3. Validates the object type (must be 1=file or 2=directory)
 * 4. Checks for read-only volume when mode 2 (write)
 * 5. Clears stale AST attributes if needed
 * 6. Sets up mapping for well-known directories (NODE, COM, WDIR, NDIR)
 *    using cached mapped info, or maps fresh via MST_$MAPS for others
 *
 * Handle fields populated:
 *   +0x0C: Directory type (0=generic, 1=NODE, 2=COM, 3=WDIR, 4=NDIR)
 *   +0x10: Object size
 *   +0x1E: Cleared to 0 on success
 *   +0x20: Mapped flag (0xFF = mapped, 0 = not mapped)
 *   +0x22: Cache slot 0 group (cleared to 0)
 *   +0x24: Cache slot 0 base address
 *   +0x2A: Cache slot count
 *   +0x2C: Cache slot 1 base address
 *   +0x3A: Volume ID
 *
 * Parameters:
 *   handle     - Pointer to handle structure
 *   mode       - Lock mode (1=read, 2=write)
 *   status_ret - Output: status code
 *
 * Returns:
 *   Internal use (value in D0)
 *
 * Original address: 0x00E4B44C
 * Original size: 622 bytes
 */

#include "dir/dir_internal.h"
#include "name/name.h"
#include "proc1/proc1.h"

/* `move.w #0x80,-(SP)` at 0x00E4B47A - the AST_$GET_COMMON_ATTRIBUTES
 * selector this site uses. */
#define DIR_CATTR_VALIDATE          0x0080

/* `cmpi.w #0x9,(-0x2,A0,D0w*0x1)` at 0x00E4B4CA against PROC1_$TYPE. */
#define DIR_PROC_TYPE_NS_HELPER     9

void DIR_$VALIDATE_HANDLE(void *handle, int16_t mode, status_$t *status_ret)
{
    uint8_t *h = (uint8_t *)handle;
    status_$t local_status;
    uint32_t map_size;

    /* A6-0x58: the object-location descriptor AST_$GET_ATTRIBUTES reads the
     * UID out of and overwrites in full on success. */
    file_$obj_loc_t desc;
    /* A6-0x70: the common-attribute summary. */
    ast_$common_attr_t cattr;

    /* 0x00E4B45C-0x00E4B466: the handle's UID goes to descriptor+0x08. */
    desc.uid.high = *(uint32_t *)(h + 0x00);
    desc.uid.low  = *(uint32_t *)(h + 0x04);
    /* 0x00E4B46A `bclr.b #0x6,(-0x3b,A6)` = descriptor+0x1D. */
    desc.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

    AST_$GET_COMMON_ATTRIBUTES(&desc, DIR_CATTR_VALIDATE, &cattr,
                               &local_status);          /* 0x00E4B482 */

    /* 0x00E4B48C-0x00E4B4B2.  descriptor+0x1D bit 7 is set when the object
     * lives on another node. */
    if (local_status != status_$ok || desc.flags < 0) {
        *status_ret = local_status;
        if (*status_ret == status_$wrong_type || *status_ret == status_$ok) {
            *status_ret = status_$naming_directory_object_not_found;  /* 0x000E0033 */
        }
        goto error;
    }

    /*
     * 0x00E4B4B6: `tst.w (-0x5a,A6)` is the WORD at cattr+0x16, whose high
     * byte is access_flags - so this is "bit 7 of access_flags", the OS-only
     * access bit.  A type-9 process (the naming server helper) may not touch
     * such an object.
     */
    if (cattr.access_flags < 0 &&
        PROC1_$TYPE[PROC1_$CURRENT] == DIR_PROC_TYPE_NS_HELPER) {  /* 0x00E4B4CA */
        /* 0x00E4B4D2 `move.l #0x3000a,(A3)` then 0x00E4B4D8
         * `bset.b #0x7,(A3)` - the error bit goes in bit 31. */
        *status_ret = (status_$t)(status_$os_only_local_access_allowed | 0x80000000u);
        goto error;
    }

    /* 0x00E4B4E0: bit 1 of the low attribute-flags byte marks a read-only
     * volume; mode 2 is the write open. */
    if ((cattr.attr_flags_lo & 0x02) != 0 && mode == 2) {
        *status_ret = status_$naming_vol_mounted_read_only;      /* 0x00E4B4EE */
        goto error;
    }

    /* 0x00E4B4FA: only sub-types 1 and 2 are directories. */
    if (cattr.sub_type != 1 && cattr.sub_type != 2) {
        *status_ret = status_$naming_branch_is_not_a_directory;  /* 0x000E000E */
        goto error;
    }

    /* 0x00E4B516: object type 0 means the object carries no attributes. */
    if (cattr.obj_type == 0) {
        *status_ret = status_$naming_bad_directory;              /* 0x000E000D */
        goto error;
    }

    /* 0x00E4B528: a non-zero block count is stale; clear attribute 0x0B. */
    if (cattr.blocks != 0) {
        uint32_t zero = 0;                                       /* 0x00E4B52E */
        AST_$SET_ATTRIBUTE((uid_t *)&desc, 0x0B, &zero, status_ret);
        if (*status_ret != status_$ok) {
            goto error_clear;
        }
    }

    /* 0x00E4B550-0x00E4B560: the directory's byte length; zero means one
     * 0x400-byte page. */
    if (cattr.length == 0) {
        *(uint32_t *)(h + 0x10) = DIR_PAGE_SIZE;
    } else {
        *(uint32_t *)(h + 0x10) = cattr.length;
    }

    /* 0x00E4B566 `move.w (-0x56,A6),(0x3a,A2)`: descriptor+0x02, i.e.
     * file_$obj_loc_t.volume, which AST_$GET_ATTRIBUTES filled from
     * aote+0x9E.  dir_$do_op_delete compares an object's own .volume against
     * this word at 0x00E5138C. */
    *(int16_t *)(h + DIR_HANDLE_VOLUME_OFF) = (int16_t)desc.volume;

    /* Check for well-known directory UIDs and use cached mapping info */

    /* Check NODE_UID */
    if (*(uint32_t *)(h + 0x00) == NAME_$NODE_UID.high &&
        *(uint32_t *)(h + 0x04) == NAME_$NODE_UID.low) {
        *(int16_t *)(h + 0x0C) = 1;
        uint32_t *info = (uint32_t *)&NAME_$NODE_MAPPED_INFO;
        *(uint32_t *)(h + 0x20) = info[0];
        *(uint32_t *)(h + 0x24) = info[1];
        *(uint32_t *)(h + 0x28) = info[2];
        *(uint32_t *)(h + 0x2C) = info[3];
        goto success;
    }

    /* Check COM_UID */
    if (*(uint32_t *)(h + 0x00) == NAME_$COM_UID.high &&
        *(uint32_t *)(h + 0x04) == NAME_$COM_UID.low) {
        *(int16_t *)(h + 0x0C) = 2;
        uint32_t *info = (uint32_t *)&NAME_$COM_MAPPED_INFO;
        *(uint32_t *)(h + 0x20) = info[0];
        *(uint32_t *)(h + 0x24) = info[1];
        *(uint32_t *)(h + 0x28) = info[2];
        *(uint32_t *)(h + 0x2C) = info[3];
        goto success;
    }

    /* Check WDIR_UID (per-address-space: base + (PROC1_$AS_ID << 3)) */
    {
        uid_t *wdir_uid = &NAME_$DATA.wdir_uid[PROC1_$AS_ID];

        if (*(uint32_t *)(h + 0x00) == wdir_uid->high &&
            *(uint32_t *)(h + 0x04) == wdir_uid->low) {
            /* mapped info slot: base + (PROC1_$AS_ID << 4) */
            name_$mapped_info_t *wdir_info = &NAME_$DATA.wdir_mapped_info[PROC1_$AS_ID];
            if (wdir_info->active < 0) {
                *(int16_t *)(h + 0x0C) = 3;
                uint32_t *info = (uint32_t *)wdir_info;
                *(uint32_t *)(h + 0x20) = info[0];
                *(uint32_t *)(h + 0x24) = info[1];
                *(uint32_t *)(h + 0x28) = info[2];
                *(uint32_t *)(h + 0x2C) = info[3];
                goto success;
            }
        }
    }

    /* Check NDIR_UID (per-address-space: base + (PROC1_$AS_ID << 3)) */
    {
        uid_t *ndir_uid = &NAME_$DATA.ndir_uid[PROC1_$AS_ID];

        if (*(uint32_t *)(h + 0x00) == ndir_uid->high &&
            *(uint32_t *)(h + 0x04) == ndir_uid->low) {
            /* mapped info slot: base + (PROC1_$AS_ID << 4) */
            name_$mapped_info_t *ndir_info = &NAME_$DATA.ndir_mapped_info[PROC1_$AS_ID];
            if (ndir_info->active >= 0) {
                goto generic_map;
            }
            *(int16_t *)(h + 0x0C) = 4;
            uint32_t *info = (uint32_t *)ndir_info;
            *(uint32_t *)(h + 0x20) = info[0];
            *(uint32_t *)(h + 0x24) = info[1];
            *(uint32_t *)(h + 0x28) = info[2];
            *(uint32_t *)(h + 0x2C) = info[3];
            goto success;
        }
    }

generic_map:
    /* Generic directory - map via MST_$MAPS */
    *(int16_t *)(h + 0x0C) = 0;
    {
        uint32_t mapped_addr;

        /* 0x00E4B656 `st -(SP)` and 0x00E4B644 `st -(SP)` push arguments 2
         * and 8 as Pascal BOOLEAN bytes (0xFF == true); the word 0xFFFF this
         * call used to pass for argument 2 was never what the callee reads. */
        MST_$MAPS(PROC1_$AS_ID, true, handle, 0, 0x10000, 0x16, 0,
                  true, &map_size, status_ret);
        /* TODO(source-qgq): MST_$MAPS returns address in A0 on m68k; assigned to handle+0x24 */
        mapped_addr = *(uint32_t *)(h + 0x24);

        if (*status_ret != status_$ok) {
            goto error_clear;
        }

        if (map_size != 0x10000) {
            CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
        }

        h[0x20] = 0xFF;     /* Mapped flag */
        *(uint16_t *)(h + 0x22) = 0;  /* Cache slot 0 group (none) */
        *(uint16_t *)(h + 0x2A) = 1;  /* Only 1 slot used */
        *(uint32_t *)(h + 0x2C) = *(uint32_t *)(h + 0x24) + 0x8000;
    }

success:
    *(uint16_t *)(h + 0x1E) = 0;
    return;

error:
    if (*status_ret == status_$ok) {
        return;
    }
error_clear:
    h[0x20] = 0;  /* Clear mapped flag */
}
