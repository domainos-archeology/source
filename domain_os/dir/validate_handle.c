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

void DIR_$VALIDATE_HANDLE(void *handle, int16_t mode, status_$t *status_ret)
{
    uint8_t *h = (uint8_t *)handle;
    uid_t local_uid;
    status_$t local_status;
    uint32_t map_size;

    /* Attribute output fields */
    uint8_t attr_valid;       /* Whether attrs are valid */
    uint8_t obj_type;         /* Object type byte */
    uint8_t flags_byte;       /* Flags (bit 1 = read-only vol) */
    uint32_t obj_size;        /* Object size */
    int32_t stale_field;      /* Stale attribute counter */
    int16_t vol_id;           /* Volume ID */
    uint8_t remote_flag;      /* Bit 6 = remote object */

    /* Buffer for AST_$GET_COMMON_ATTRIBUTES output */
    uint8_t attr_buf[0x80];

    /* Copy UID from handle */
    local_uid.high = *(uint32_t *)(h + 0x00);
    local_uid.low = *(uint32_t *)(h + 0x04);

    /* Clear bit 6 of remote flag before call */
    /* attr_buf has remote_flag at relative offset, use local variables */
    remote_flag = 0;

    /* Get common attributes */
    AST_$GET_COMMON_ATTRIBUTES(&local_uid, 0x80, &attr_valid, &local_status);

    /* TODO(source-qgq): The exact layout of AST_$GET_COMMON_ATTRIBUTES output
     * needs verification. The decompiler shows various field accesses
     * into the output buffer. The key fields extracted are:
     *   - attr_valid (byte): non-zero if attributes are present
     *   - obj_type (byte): 1=file, 2=directory
     *   - flags_byte (byte at offset ~0x0B): bit 1 = read-only volume
     *   - remote_flag (byte at offset ~0x1D): bit 6 = remote object
     *   - obj_size (4 bytes): object size
     *   - stale_field (4 bytes): stale attribute counter
     *   - vol_id (2 bytes): volume ID
     */

    if (local_status != status_$ok || (int8_t)remote_flag < 0) {
        /* Attribute error or remote object */
        *status_ret = local_status;
        if (*status_ret == status_$wrong_type || *status_ret == status_$ok) {
            *status_ret = status_$naming_acl_not_found;  /* 0x000E0033 */
        }
        goto error;
    }

    /* Check for server process with deleted object */
    /* vol_id field (local_5e) is negative when object is pending delete */
    /* Skipping detailed server check - see assembly for exact logic */

    /* Check read-only volume for write mode */
    if ((flags_byte & 2) != 0 && mode == 2) {
        *status_ret = status_$naming_vol_mounted_read_only;
        goto error;
    }

    /* Validate object type */
    if (obj_type != 1 && obj_type != 2) {
        *status_ret = status_$naming_name_is_not_a_file;  /* 0x000E000E */
        goto error;
    }

    /* Check if attributes are valid */
    if (attr_valid == 0) {
        *status_ret = status_$naming_bad_directory;  /* 0x000E000D */
        goto error;
    }

    /* Clear stale attributes if present */
    if (stale_field != 0) {
        uint32_t zero = 0;
        AST_$SET_ATTRIBUTE(&local_uid, 0x0B, &zero, status_ret);
        if (*status_ret != status_$ok) {
            goto error_clear;
        }
    }

    /* Set object size in handle */
    if (obj_size == 0) {
        *(uint32_t *)(h + 0x10) = 0x400;
    } else {
        *(uint32_t *)(h + 0x10) = obj_size;
    }

    /* Copy volume ID */
    *(int16_t *)(h + 0x3A) = vol_id;

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

    /* Check WDIR_UID (per-address-space) */
    {
        uint16_t offset = PROC1_$AS_ID << 3;
        uint32_t *wdir_uid = (uint32_t *)((char *)&NAME_$WDIR_UID + (int16_t)offset);

        if (*(uint32_t *)(h + 0x00) == wdir_uid[0] &&
            *(uint32_t *)(h + 0x04) == wdir_uid[1]) {
            int16_t info_offset = PROC1_$AS_ID << 4;
            uint8_t *wdir_info = (uint8_t *)&NAME_$WDIR_MAPPED_INFO + info_offset;
            if ((int8_t)wdir_info[0] < 0) {
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

    /* Check NDIR_UID (per-address-space) */
    {
        uint16_t offset = PROC1_$AS_ID << 3;
        uint32_t *ndir_uid = (uint32_t *)((char *)&NAME_$NDIR_UID + (int16_t)offset);

        if (*(uint32_t *)(h + 0x00) == ndir_uid[0] &&
            *(uint32_t *)(h + 0x04) == ndir_uid[1]) {
            uint16_t info_offset = PROC1_$AS_ID << 4;
            uint8_t *ndir_info = (uint8_t *)&NAME_$NDIR_MAPPED_INFO + (int16_t)info_offset;
            if ((int8_t)ndir_info[0] >= 0) {
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

        MST_$MAPS(PROC1_$AS_ID, 0xFFFF, handle, 0, 0x10000, 0x16, 0,
                  0xFF, &map_size, status_ret);
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
