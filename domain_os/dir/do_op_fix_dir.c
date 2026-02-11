/*
 * dir_$do_op_fix_dir - Server-side handler for FIX_DIR op
 *
 * Server-side handler for opcode 0x48 in DIR_$DO_OP. Rebuilds a
 * directory by creating a new empty directory, iterating over all
 * entries in the original, adding each entry to the new directory,
 * then replacing the original's page data with the new directory's
 * data.
 *
 * Process:
 * 1. Enter supervisor mode
 * 2. Open original directory (mode=0, rights=2 - read)
 * 3. Read page 0 header, save ACL UIDs from offsets 0x46 and 0x7A
 * 4. Create a new temporary directory via FUN_00e52394
 * 5. Open the new directory for writing
 * 6. For each page in the original:
 *    a. Check page is valid (entry type 0, matching self-index, entry count 5)
 *    b. Parse the entry table and directory entries
 *    c. For each entry: extract name, type, UID, link data
 *    d. Add to new directory via FUN_00e4fe0a
 * 7. Truncate original to zero, copy new directory pages over
 * 8. Restore saved ACL UIDs in page 0
 * 9. Truncate to new size, write file, restore entry type
 * 10. Clean up handles, delete temp directory if created
 *
 * Called by DIR_$DO_OP case 0x48.
 *
 * Entry types in the directory page entries:
 *   Type 2: Normal file entry (UID at +4..+11)
 *   Type 3: Hard link entry (UID at +4..+11, extra at +12..+15)
 *   Type 4: Soft link entry (link length at +2, data follows or in overflow)
 *
 * Parameters:
 *   dir_uid    - UID of directory to fix/rebuild
 *   status_ret - Output: status code
 *
 * Original address: 0x00E53A18
 * Original size: 1132 bytes
 *
 * TODO: The entry parsing logic is complex. The entry table starts at
 * offset 0x12 (or 0x12 + page_header_offset for page 0). Each entry
 * index is a 2-byte offset from the page base. Entry format:
 *   +0: type (bits 2-0) | flags (bits 7-3)
 *   +1: name length
 *   +2: varies by type (link_len for type 4, unused for type 2/3)
 *   +4: UID or link data
 * The name follows after the fixed header at an offset determined by
 * a per-type size table at A5+0x2000.
 */

#include "dir/dir_internal.h"

/*
 * Directory page constants
 */
#define DIR_PAGE_SIZE           0x400   /* 1024 bytes per page */
#define DIR_MAX_NAME_LEN        256
#define DIR_ENTRY_TYPE_MASK     0x07
#define DIR_ENTRY_TYPE_FILE     2
#define DIR_ENTRY_TYPE_LINK     3
#define DIR_ENTRY_TYPE_SOFTLINK 4

/*
 * Per-type entry header size table offset (A5-relative)
 * Located at A5+0x2000, indexed by entry_type * 2
 */
#define DIR_TYPE_SIZE_TABLE_OFF 0x2000

/* DAT_00e52040 - truncation size parameter (value = 0x00000400, one page)
 * In the original binary, this is a 32-bit constant at address 0x00E52040.
 */
static const uint32_t DAT_00e52040 = 0x00000400;

void dir_$do_op_fix_dir(uid_t *dir_uid, status_$t *status_ret)
{
    char *a5 = (char *)__A5_BASE();
    uint32_t orig_handle = 0;
    uint32_t new_handle = 0;
    uid_t new_dir_uid;
    uint32_t saved_acl1_high, saved_acl1_low;  /* ACL at offset 0x46 */
    uint32_t saved_acl2_high, saved_acl2_low;  /* ACL at offset 0x7A */
    uint16_t total_pages;
    int16_t page_idx;
    status_$t temp_status;
    uint8_t name_buf[DIR_MAX_NAME_LEN];

    new_dir_uid.high = UID_$NIL.high;
    new_dir_uid.low = UID_$NIL.low;

    ACL_$ENTER_SUPER();

    /* Open original directory (mode=0 read-only, rights=2) */
    FUN_00e4ba02(dir_uid, 0, 2, &orig_handle, status_ret);
    if (*status_ret != status_$ok) {
        goto done;
    }

    /* Read page 0 header and save ACL UIDs */
    {
        uint8_t *page0 = (uint8_t *)FUN_00e4b340((void *)(uintptr_t)orig_handle, 0);

        /* Save directory ACL UID (offset 0x46) */
        saved_acl1_high = *(uint32_t *)(page0 + 0x46);
        saved_acl1_low = *(uint32_t *)(page0 + 0x4A);

        /* Save file ACL UID (offset 0x7A) */
        saved_acl2_high = *(uint32_t *)(page0 + 0x7A);
        saved_acl2_low = *(uint32_t *)(page0 + 0x7E);

        /* Create new temporary directory */
        FUN_00e52394(&UID_$NIL, page0, &UID_$NIL, &UID_$NIL,
                     &new_dir_uid, status_ret);
    }

    if (*status_ret != status_$ok) {
        goto done;
    }

    /* Open new directory for writing */
    FUN_00e4ba02(&new_dir_uid, 2, 0, &new_handle, status_ret);
    if (*status_ret != status_$ok) {
        goto done;
    }

    /*
     * Iterate over all pages in the original directory and copy
     * valid entries to the new directory.
     */
    total_pages = (uint16_t)((*(uint32_t *)((uintptr_t)orig_handle + 0x10)) >> 10) - 1;
    page_idx = 0;

    {
        uint16_t pages_remaining = total_pages;

        do {
            uint8_t *page = (uint8_t *)FUN_00e4b340((void *)(uintptr_t)orig_handle, page_idx);
            uint8_t entry_type_field = page[0] >> 6;

            /* Only process valid normal pages (type 0) */
            if (entry_type_field == 0 &&
                page_idx == *(int16_t *)(page + 10) &&
                (page[1] & 0x3F) == 5) {

                uint8_t *page_end = page + DIR_PAGE_SIZE;
                int16_t entry_table_off;
                int16_t num_entries;
                int16_t entry_idx;
                uint8_t *entry_table;

                /* Entry table offset depends on whether this is page 0 */
                if (page_idx == 0) {
                    entry_table_off = *(int16_t *)(page + 0x14) + 0x12;
                } else {
                    entry_table_off = 0x12;
                }

                if (entry_table_off >= DIR_PAGE_SIZE) {
                    goto next_page;
                }

                /* Calculate number of entries from the entry table */
                {
                    int32_t diff = (int32_t)*(int16_t *)(page + 0x0E)
                                   - (int32_t)entry_table_off;
                    if (diff < 0) {
                        diff += 1;
                    }
                    num_entries = (int16_t)(diff >> 1);
                }

                if (num_entries >= DIR_PAGE_SIZE) {
                    goto next_page;
                }

                entry_table = page + entry_table_off;
                if (entry_table >= page_end) {
                    goto next_page;
                }

                {
                    uint16_t entries_remaining = num_entries - 1;
                    if ((int16_t)entries_remaining < 0) {
                        goto next_page;
                    }

                    entry_idx = 1;
                    uint8_t *tbl_ptr = entry_table;

                    do {
                        /* Skip first entry on page 0 (root entry) */
                        if (page_idx != 0 || entry_idx != 1) {
                            uint8_t *entry;
                            int16_t entry_off = *(int16_t *)tbl_ptr;

                            entry = page + entry_off;
                            if (entry + 2 >= page_end) {
                                goto next_entry;
                            }

                            uint8_t etype = entry[0] & DIR_ENTRY_TYPE_MASK;
                            if (etype >= 8) {
                                goto next_entry;
                            }

                            /* Look up per-type header size */
                            int16_t hdr_size = *(int16_t *)(a5
                                + DIR_TYPE_SIZE_TABLE_OFF
                                + (int16_t)(etype * 2));
                            int16_t entry_size = (uint8_t)entry[1] + hdr_size;

                            if ((int)(entry + entry_size) > (int)page_end) {
                                goto next_entry;
                            }

                            /* Handle type 4 (soft link) inline overflow */
                            if (etype == DIR_ENTRY_TYPE_SOFTLINK &&
                                *(int16_t *)(entry + 4) == -1) {
                                entry_size += *(int16_t *)(entry + 2);
                            }

                            if ((int)(entry + entry_size) > (int)page_end) {
                                goto next_entry;
                            }

                            /* Validate name length */
                            {
                                uint8_t name_len = entry[1];
                                if (name_len >= DIR_MAX_NAME_LEN) {
                                    goto next_entry;
                                }

                                /* Copy name to local buffer */
                                {
                                    int16_t name_off = hdr_size;
                                    uint16_t n = name_len - 1;
                                    if ((int16_t)n >= 0) {
                                        int16_t k;
                                        for (k = 1; n != 0xFFFF; k++, n--) {
                                            name_buf[k - 1] =
                                                entry[k + name_off - 1];
                                        }
                                    }
                                }

                                /* Build parameters for FUN_00e4fe0a (add entry) */
                                {
                                    uint32_t extra = 0;
                                    uint16_t link_len = 0;
                                    uint32_t entry_uid_high;
                                    uint32_t entry_uid_low;
                                    uint8_t *link_data = NULL;

                                    if (etype == DIR_ENTRY_TYPE_FILE) {
                                        /* Type 2: file entry - UID at +4 */
                                        entry_uid_high = *(uint32_t *)(entry + 4);
                                        entry_uid_low = *(uint32_t *)(entry + 8);
                                    }
                                    else if (etype == DIR_ENTRY_TYPE_LINK) {
                                        /* Type 3: hard link - UID at +4, extra at +12 */
                                        entry_uid_high = *(uint32_t *)(entry + 4);
                                        entry_uid_low = *(uint32_t *)(entry + 8);
                                        extra = *(uint32_t *)(entry + 0x0C);
                                    }
                                    else if (etype == DIR_ENTRY_TYPE_SOFTLINK) {
                                        /* Type 4: soft link */
                                        if (*(int16_t *)(entry + 2) > 0x3FF) {
                                            goto next_entry;
                                        }
                                        entry_uid_high = UID_$NIL.high;
                                        entry_uid_low = UID_$NIL.low;
                                        link_len = *(uint16_t *)(entry + 2);

                                        if (*(int16_t *)(entry + 4) == -1) {
                                            /* Inline link data */
                                            link_data = entry + entry[1] + 0x0C;
                                        } else {
                                            /* Link data in overflow page */
                                            uint16_t ovf_page =
                                                *(uint16_t *)(entry + 4);
                                            if (ovf_page > total_pages) {
                                                goto next_entry;
                                            }
                                            uint8_t *ovf =
                                                (uint8_t *)FUN_00e4b340(
                                                    (void *)(uintptr_t)orig_handle, ovf_page);
                                            if (ovf[0] >> 6 != 2) {
                                                goto next_entry;
                                            }
                                            link_data = ovf + 1;
                                        }
                                    }
                                    else {
                                        goto next_entry;
                                    }

                                    /* Add entry to new directory */
                                    {
                                        uid_t entry_uid;
                                        entry_uid.high = entry_uid_high;
                                        entry_uid.low = entry_uid_low;

                                        FUN_00e4fe0a(new_handle, name_buf,
                                                     name_len, etype, extra,
                                                     &entry_uid, link_len,
                                                     link_data, status_ret);

                                        if (*status_ret != status_$ok &&
                                            *status_ret != status_$name_already_exists) {
                                            goto done;
                                        }
                                    }
                                }
                            }
                        }

next_entry:
                        entry_idx++;
                        entries_remaining--;
                        tbl_ptr += 2;
                    } while (entries_remaining != 0xFFFF);
                }
            }

next_page:
            page_idx++;
            pages_remaining--;
        } while (pages_remaining != 0xFFFF);
    }

    /*
     * Phase 2: Replace original directory contents with new directory.
     *
     * Truncate original to zero, copy pages from new to original,
     * restore ACL UIDs, then truncate to correct size.
     */
    FILE_$TRUNCATE((uid_t *)(uintptr_t)orig_handle, (uint32_t *)&DAT_00e52040, status_ret);
    if (*status_ret != status_$ok) {
        goto done;
    }

    {
        int16_t copy_page = 0;
        uint32_t new_total = (*(uint32_t *)((uintptr_t)new_handle + 0x10) >> 10) - 1;

        do {
            /* Read corresponding pages from both directories */
            uint16_t *orig_page = (uint16_t *)FUN_00e4b340((void *)(uintptr_t)orig_handle, copy_page);
            uint32_t *new_page = (uint32_t *)FUN_00e4b340((void *)(uintptr_t)new_handle, copy_page);

            /* Copy 256 uint32_t (1024 bytes = one page) from new to original */
            {
                int16_t n;
                uint32_t *src = new_page;
                uint16_t *dst = orig_page;
                for (n = 0xFF; n >= 0; n--) {
                    *(uint32_t *)dst = *src;
                    src++;
                    dst += 2;  /* uint16_t pointer, advance by 4 bytes */
                }
            }

            if (copy_page == 0) {
                /* Restore saved ACL UIDs in page 0 */
                /* offset 0x46 = uint16_t index 0x23 */
                *(uint32_t *)&orig_page[0x23] = saved_acl1_high;
                *(uint32_t *)&orig_page[0x25] = saved_acl1_low;
                /* offset 0x7A = uint16_t index 0x3D */
                *(uint32_t *)&orig_page[0x3D] = saved_acl2_high;
                *(uint32_t *)&orig_page[0x3F] = saved_acl2_low;

                /* Clear entry type bits (bits 5-0 of word 0) */
                *orig_page &= 0xFFC0;

                /* Force write page 0 */
                FILE_$FW_FILE((uid_t *)(uintptr_t)orig_handle, status_ret);
                if (*status_ret != status_$ok) {
                    goto done;
                }
            }

            copy_page++;
            new_total--;
        } while ((int16_t)new_total != -1);

        /* Truncate original to match new directory size */
        FILE_$TRUNCATE((uid_t *)(uintptr_t)orig_handle, (uint32_t *)((uintptr_t)new_handle + 0x10), status_ret);
        if (*status_ret != status_$ok) {
            goto done;
        }

        /* Force write the truncated file */
        FILE_$FW_FILE((uid_t *)(uintptr_t)orig_handle, status_ret);
        if (*status_ret != status_$ok) {
            goto done;
        }

        /* Restore entry type to 5 (directory) in page 0 */
        {
            uint8_t *page0 = (uint8_t *)FUN_00e4b340((void *)(uintptr_t)orig_handle, 0);
            page0[1] = (page0[1] & 0xC0) | 5;
            FILE_$FW_FILE((uid_t *)(uintptr_t)orig_handle, status_ret);
        }
    }

done:
    /* Release both handles */
    FUN_00e4b9d6(&orig_handle);
    FUN_00e4b9d6(&new_handle);

    /* If we created a temporary directory, decrement its reference count */
    if (new_dir_uid.high != UID_$NIL.high ||
        new_dir_uid.low != UID_$NIL.low) {
        FILE_$SET_REFCNT(&new_dir_uid, (uint32_t *)(a5 - 8), &temp_status);
    }

    ACL_$EXIT_SUPER();
}
