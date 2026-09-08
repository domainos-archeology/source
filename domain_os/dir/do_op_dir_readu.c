/*
 * dir_$do_op_dir_readu - DO_OP handler for directory read
 *
 * Local handler for the DIR_READU operation (opcode 0x42). Opens the
 * directory, reads entries from the B-tree pages, and formats them
 * into the output buffer. Handles both old-format (version 0) and
 * new-format (version > 0) response entries.
 *
 * Supports two response formats depending on the protocol version:
 *   Version 0: compact format (type, UID, name)
 *   Version > 0: extended format (size, cont, name_len, name, type, UID, extra)
 *
 * Special handling for "." and ".." virtual entries at the beginning
 * of the listing. "." is represented by continuation value 0x2118(A5)
 * and ".." by 0x2114(A5).
 *
 * For the ".." entry, if the directory is NODE_UID, resolves to ROOT_UID.
 * Otherwise calls FILE_$GET_ATTRIBUTES to get the parent UID.
 *
 * Includes an entry remapping table (at A5+0x155A..0x15A0) that maps
 * certain UIDs to their network-redirected equivalents in the response.
 *
 * Parameters:
 *   uid        - Directory UID
 *   version    - Protocol version (0 = old, >0 = new)
 *   name       - Starting entry name (for continuation)
 *   name_flags - Entry flags
 *   cont       - Continuation pointer (in/out)
 *   max_entries - maximum entries, a LONGWORD (`move.l (0x18,A6),D5`
 *                 at 0x00E4D968, compared with `cmp.l (A4),D5`)
 *   max_size   - Maximum response buffer size (bytes)
 *   buf_ptr    - the caller's entry buffer (`movea.l (0x20,A6),A1`
 *                at 0x00E4DA9A); DIR_$DO_OP passes request+0x9A
 *   size_ret   - Output: actual data size (bytes)
 *   offset_ret - Output: offset to last entry
 *   count_ret  - Output: number of entries returned
 *   status_ret - Output: status code
 *
 * Original address: 0x00E4D954
 * Original size: 1414 bytes
 *
 * TODO(source-qgq): This is one of the most complex functions in the directory subsystem.
 * The decompilation has many artifacts from the M68K register usage and
 * Pascal nested procedure conventions. The entry formatting logic for
 * both version 0 and version > 0 needs careful verification against
 * the assembly. The "."/".." handling and the UID remapping table
 * access patterns should be double-checked.
 */

#include "dir/dir_internal.h"

/* DIR_$READU_ATTR_SIZE - Attribute parameter for FILE_$GET_ATTRIBUTES (0x0090) */

/* DIR_$READU_NUL_NAME - NUL byte used as 1-char name for dir_$find_entry("\0", 1) */

/* dir_$next_page - Advance to the next page in B-tree traversal */
void dir_$next_page(void *handle, int16_t depth, void *extra, uint16_t *page_ret);

/* Name offset table */

void dir_$do_op_dir_readu(uid_t *uid, int16_t version, char *name,
                          uint16_t name_flags, void *cont_ptr,
                          uint32_t max_entries, uint32_t max_size,
                          void *buf_ptr, void *size_ret_ptr,
                          void *offset_ret_ptr, void *count_ret_ptr,
                          status_$t *status_ret)
{
    /* Cast void* parameters to typed pointers for internal use */
    uint16_t *cont = (uint16_t *)cont_ptr;
    int32_t *size_ret = (int32_t *)size_ret_ptr;
    int32_t *offset_ret = (int32_t *)offset_ret_ptr;
    int32_t *count_ret = (int32_t *)count_ret_ptr;

    char *a5 = (char *)__A5_BASE();
    int16_t local_name_len;
    int32_t word_idx;       /* Running output word index */
    uint16_t *last_entry;   /* Pointer to last written entry */
    int8_t is_old_version;  /* version==0 flag */
    int8_t at_eof;          /* EOF flag */
    int8_t overflow;        /* Buffer overflow flag */
    uint32_t local_handle;
    uint16_t entry_size;
    int16_t depth;
    uint16_t page_idx;
    uint16_t entry_idx;
    uint8_t *page_data;
    uint8_t *entry_ptr;
    uint8_t *entry_name;
    uint8_t *base_ptr;
    uint8_t *idx_ptr;
    int16_t base_offset;
    int16_t num_entries;
    void *find_entry_ret;
    uint16_t extra_array[18];
    /* The 0x20-byte object-location record FILE_$GET_ATTRIBUTES rewrites. */
    file_$obj_loc_t attr_buf1;
    /* FILE_$GET_ATTRIBUTES copies 36 longwords into this buffer
     * unconditionally (0x00E5DA20), so it is a full 0x90 bytes. */
    uint8_t attr_buf2[AST_ATTR_REC_SIZE];
    uint32_t parent_uid_high;
    uint32_t parent_uid_low;

    depth = 0;
    *count_ret = 0;
    word_idx = 0;
    *status_ret = status_$ok;
    last_entry = (uint16_t *)0;
    *size_ret = 0;
    *offset_ret = 0;
    local_name_len = name_flags;
    is_old_version = -(version == 0);
    at_eof = 0;
    overflow = 0;

    if (max_entries == 0) return;
    if (max_size == 0) return;
    if (*(int32_t *)cont == 0 && name_flags == 0) return;

    ACL_$ENTER_SUPER();

    /* Open directory with read access, ACL right 4 */
    dir_$open_dir(uid, 1, 4, &local_handle, status_ret);
    if (*status_ret != status_$ok) goto exit_cleanup;

    /* Handle protocol version differences for starting position */
    if (is_old_version < 0) {
        /* Old version: special continuation values */
        if (local_name_len == 0x20) {
            local_name_len = 0;
        }
        if (local_name_len == 0) {
            /* Check if continuation matches a special A5-relative value */
            if (*(int32_t *)cont == *(int32_t *)(a5 + 0x211C)) {
                *(int32_t *)cont = *(int32_t *)(a5 + 0x2114);
            }
        }
    } else {
        /* New version */
        if (local_name_len != 0) {
            /* Named continuation */
            if (local_name_len == 1 && name[0] == '.') {
                /* "." -> dot continuation */
                *(int32_t *)cont = *(int32_t *)(a5 + 0x2118);
            } else if (local_name_len == 2 && name[0] == '.' && name[1] == '.') {
                /* ".." -> parent continuation */
                *(int32_t *)cont = *(int32_t *)(a5 + 0x2114);
            } else {
                goto start_named_search;
            }
            local_name_len = 0;
        }
    }

    /* Check for dot/dotdot virtual entries */
    if (local_name_len == 0) {
        if (*(int32_t *)cont == *(int32_t *)(a5 + 0x211C) ||
            *(int32_t *)cont == *(int32_t *)(a5 + 0x2118)) {
            /* Emit "." and ".." virtual entries */
            do {
                uint16_t virt_name_len;
                if (*(int32_t *)cont == *(int32_t *)(a5 + 0x211C)) {
                    virt_name_len = 1;  /* "." */
                } else {
                    virt_name_len = 2;  /* ".." */
                }

                entry_size = (virt_name_len + 0x1A) & 0xFFFC;

                /* Check buffer limits */
                if (max_size < (uint32_t)(word_idx * 2 + entry_size) ||
                    max_entries == *count_ret) {
                    overflow = -1;
                    goto exit_cleanup;
                }

                last_entry = (uint16_t *)((char *)buf_ptr + word_idx * 2);
                word_idx += (entry_size >> 1);
                *count_ret = *count_ret + 1;

                /* Format entry */
                last_entry[0] = entry_size;
                *(int32_t *)(last_entry + 8) = *(int32_t *)cont;
                last_entry[10] = virt_name_len;

                /* Write dots */
                {
                    uint16_t k;
                    for (k = 0; k < virt_name_len; k++) {
                        *((uint8_t *)last_entry + 0x16 + k) = '.';
                    }
                }
                *((uint8_t *)last_entry + 0x16 + virt_name_len) = '\0';

                /* Entry type = 1 (directory) */
                last_entry[1] = 1;

                /* Resolve UID for ".." */
                if (*(int32_t *)cont == *(int32_t *)(a5 + 0x2118)) {
                    /* ".." entry - get parent UID */
                    if (uid->high == NAME_$NODE_UID.high &&
                        uid->low == NAME_$NODE_UID.low) {
                        /* //node_data -> root */
                        uid_t *root = &NAME_$ROOT_UID;
                        *(uint32_t *)(last_entry + 2) = root->high;
                        *(uint32_t *)(last_entry + 4) = root->low;
                    } else {
                        FILE_$GET_ATTRIBUTES(uid, &DIR_$CONST_ONE_W,
                                             &DIR_$READU_ATTR_SIZE, &attr_buf1,
                                             attr_buf2, status_ret);
                        if (*status_ret != status_$ok) goto exit_cleanup;
                        /* Parent UID is at offset 0x60-0x5c from attr_buf2 base */
                        *(uint32_t *)(last_entry + 2) = *(uint32_t *)(attr_buf2 + 0x3C);
                        *(uint32_t *)(last_entry + 4) = *(uint32_t *)(attr_buf2 + 0x40);
                    }
                } else {
                    /* "." entry - use directory's own UID */
                    *(uint32_t *)(last_entry + 2) = uid->high;
                    *(uint32_t *)(last_entry + 4) = uid->low;
                }

                /* Clear extra fields */
                last_entry[6] = 0;
                last_entry[7] = 0;

                /* Advance continuation */
                cont[0] = cont[0] + 1;

            } while (*(int32_t *)cont != *(int32_t *)(a5 + 0x2114));
        }
    }

    if (overflow < 0) goto exit_cleanup;

    /* Set up starting position for B-tree traversal */
    if (local_name_len != 0) {
start_named_search:
        /* Named starting position - find it in the B-tree */
        dir_$find_entry((void *)local_handle, name, local_name_len,
                        0x20, &find_entry_ret, extra_array + 2, &depth);
        page_idx = extra_array[depth * 2];
        entry_idx = extra_array[depth * 2 + 1] + 1;
    } else {
        /* Continuation-based starting position */
        if (cont[1] != 0) {
            page_idx = cont[1] - 1;
        } else {
            page_idx = 0;
        }
        entry_idx = cont[0];

        /* Validate page_idx doesn't exceed directory size */
        {
            int32_t total_size = *(int32_t *)((char *)local_handle + 0x10) - 1;
            if (total_size < 0) {
                total_size += 0x3FF;
            }
            if ((total_size >> 10) < (int32_t)(uint32_t)page_idx) {
                goto exit_cleanup;
            }
        }

        /* For page 0 with entry_idx < 3, find the first entry via dir_$find_entry */
        if (page_idx == 0 && (int16_t)entry_idx < 3) {
            dir_$find_entry((void *)local_handle, &DIR_$READU_NUL_NAME, 1,
                            0x20, &find_entry_ret, extra_array + 2, &depth);
            page_idx = extra_array[depth * 2];
            entry_idx = extra_array[depth * 2 + 1];
        }

        if (entry_idx == 0) {
            entry_idx = 1;
        }
    }

    /* Skip entry (0, 1) which is the root self-entry */
    if (page_idx == 0 && entry_idx == 1) {
        entry_idx = 2;
    }

    /* Main entry reading loop - iterate through pages */
    for (;;) {
        int16_t iVar;

        page_data = (uint8_t *)dir_$map_page((void *)local_handle, page_idx);
        if (*(int16_t *)page_data == 0) break;  /* Empty page */
        if ((*page_data >> 6) != 0) break;      /* Not a leaf page */

        /* Compute base offset and entry count */
        if (*(int16_t *)(page_data + 10) == 0) {
            base_offset = *(int16_t *)(page_data + 0x14) + 0x12;
        } else {
            base_offset = 0x12;
        }
        iVar = (int)(*(int16_t *)(page_data + 0x0E)) - (int)base_offset;
        if (iVar < 0) iVar += 1;
        num_entries = (int16_t)(iVar >> 1);

        base_ptr = page_data + base_offset;
        idx_ptr = base_ptr + entry_idx * 2;

        /* Iterate through entries on this page */
        while (entry_idx <= num_entries) {
            entry_ptr = page_data + *(int16_t *)(idx_ptr - 2);

            /* Compute output entry size */
            if (is_old_version < 0) {
                /* Old format: name_len + 0x10, word-aligned */
                entry_size = entry_ptr[1] + 0x10;
                if (entry_size & 1) entry_size++;
            } else {
                /* New format: name_len + 0x1A, 4-byte aligned */
                entry_size = (entry_ptr[1] + 0x1A) & 0xFFFC;
            }

            /* Check buffer limits */
            if (max_size < (uint32_t)(word_idx * 2 + entry_size) ||
                max_entries == *count_ret) {
                overflow = -1;
                goto finalize;
            }

            /* Get name pointer for this entry */
            entry_name = entry_ptr + DIR_$NAME_OFFSET_TABLE[(*entry_ptr & 7)];
            last_entry = (uint16_t *)((char *)buf_ptr + word_idx * 2);
            word_idx += (entry_size >> 1);
            *count_ret = *count_ret + 1;

            if (is_old_version >= 0) {
                /* New format output */
                last_entry[0] = entry_size;
                cont[1] = page_idx + 1;
                cont[0] = entry_idx;
                *(int32_t *)(last_entry + 8) = *(int32_t *)cont;
                last_entry[10] = (uint16_t)(uint8_t)entry_ptr[1];

                /* Copy name */
                {
                    int16_t nlen = (int16_t)(uint8_t)entry_ptr[1] - 1;
                    if (nlen >= 0) {
                        int16_t j = 1;
                        do {
                            *((uint8_t *)last_entry + j + 0x15) = entry_name[j - 1];
                            j++;
                            nlen--;
                        } while (nlen != -1);
                    }
                }
                *((uint8_t *)last_entry + (int16_t)last_entry[10] + 0x16) = '\0';

                /* Format type-specific fields.  Jump table at 0x00E4DE7E,
                 * bounded by `cmpi.w #0x5,D0w` / `bcc` at 0x00E4DE6C: types
                 * 0 and 1 land on the CRASH_SYSTEM default at 0x00E4DF48,
                 * type 2 on 0x00E4DE88, type 3 on 0x00E4DEFC and type 4 on
                 * 0x00E4DF2A. */
                switch (*entry_ptr & 7) {
                case 2: /* File/directory entry */
                    last_entry[1] = 1;
                    *(uint32_t *)(last_entry + 2) = *(uint32_t *)(entry_ptr + 4);
                    *(uint32_t *)(last_entry + 4) = *(uint32_t *)(entry_ptr + 8);
                    /* 0x00E4DEAA-0x00E4DEFA: substitute a mounted volume's
                     * root uid.  The scan walks the ONE-BASED mount tables
                     * (see DIR_MOUNT_UID_TAB_OFF): `lea (0x1554,A0),A2`
                     * with A0 = A5 + 8 + 8k is UID_TAB[k+1], and the
                     * replacement uses `lsl.l #0x3` on the 1-based index to
                     * reach TGT_TAB[n]. */
                    {
                        int16_t remaining =
                            (int16_t)(DIR_MOUNT_COUNT16(a5) - 1);
                        int16_t n;

                        for (n = 1; remaining >= 0; n++, remaining--) {
                            if (*(uint32_t *)(last_entry + 2) ==
                                    *(uint32_t *)(a5 + DIR_MOUNT_UID_TAB_OFF + n * 8) &&
                                *(uint32_t *)(last_entry + 4) ==
                                    *(uint32_t *)(a5 + DIR_MOUNT_UID_TAB_OFF + n * 8 + 4)) {
                                *(uint32_t *)(last_entry + 2) =
                                    *(uint32_t *)(a5 + DIR_MOUNT_TGT_TAB_OFF + n * 8);
                                *(uint32_t *)(last_entry + 4) =
                                    *(uint32_t *)(a5 + DIR_MOUNT_TGT_TAB_OFF + n * 8 + 4);
                                break;
                            }
                        }
                    }
                    break;

                case 3: /* File/dir with extra field */
                    last_entry[1] = 1;
                    *(uint32_t *)(last_entry + 2) = *(uint32_t *)(entry_ptr + 4);
                    *(uint32_t *)(last_entry + 4) = *(uint32_t *)(entry_ptr + 8);
                    *(uint32_t *)(last_entry + 6) = *(uint32_t *)(entry_ptr + 0x0C);
                    break;

                case 4: /* Link entry */
                    last_entry[1] = 3;
                    *(uint32_t *)(last_entry + 2) = UID_$NIL.high;
                    *(uint32_t *)(last_entry + 4) = UID_$NIL.low;
                    last_entry[6] = 0;
                    last_entry[7] = 0;
                    break;

                default:
                    CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
                    break;
                }
            } else {
                /* Old format output */
                last_entry[1] = (uint16_t)(uint8_t)entry_ptr[1];

                /* Copy name to old format offset */
                {
                    int16_t nlen = (int16_t)(uint8_t)entry_ptr[1] - 1;
                    if (nlen >= 0) {
                        int16_t j = 1;
                        do {
                            *((uint8_t *)last_entry + j + 0x0F) = entry_name[j - 1];
                            j++;
                            nlen--;
                        } while (nlen != -1);
                    }
                }

                /* Format type-specific fields.  Jump table at 0x00E4DDB6,
                 * bounded by `cmpi.w #0x5,D6w` / `bcc` at 0x00E4DDA4: types
                 * 0 and 1 land on the CRASH_SYSTEM default at 0x00E4DF48,
                 * type 2 on 0x00E4DDC0 (which falls into the `clr.l (0xc,A0)`
                 * at 0x00E4DE04 that type 4 also uses), type 3 on 0x00E4DDD6
                 * and type 4 on 0x00E4DDF2.  This short form does NOT do the
                 * mount substitution the long form does. */
                switch (*entry_ptr & 7) {
                case 2: /* File/directory entry */
                    last_entry[0] = 1;
                    *(uint32_t *)(last_entry + 2) = *(uint32_t *)(entry_ptr + 4);
                    *(uint32_t *)(last_entry + 4) = *(uint32_t *)(entry_ptr + 8);
                    last_entry[6] = 0;
                    last_entry[7] = 0;
                    break;

                case 3: /* File/dir with extra field */
                    last_entry[0] = 1;
                    *(uint32_t *)(last_entry + 2) = *(uint32_t *)(entry_ptr + 4);
                    *(uint32_t *)(last_entry + 4) = *(uint32_t *)(entry_ptr + 8);
                    *(uint32_t *)(last_entry + 6) = *(uint32_t *)(entry_ptr + 0x0C);
                    break;

                case 4: /* Link entry */
                    last_entry[0] = 3;
                    *(uint32_t *)(last_entry + 2) = UID_$NIL.high;
                    *(uint32_t *)(last_entry + 4) = UID_$NIL.low;
                    last_entry[6] = 0;
                    last_entry[7] = 0;
                    break;

                default:
                    CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
                    break;
                }
            }

            entry_idx++;
            idx_ptr += 2;
        }

        /* Move to next page. When depth==0, re-find the last entry
         * in the B-tree to establish position for dir_$next_page. */
        if (depth == 0) {
            int8_t refind_eof = 0;
            dir_$refind_entry(local_handle, page_data, base_ptr,
                             &find_entry_ret, (void **)&entry_name,
                             extra_array + 2, &depth,
                             num_entries, &refind_eof);
            if ((int8_t)refind_eof < 0) {
                break;
            }
        }
        dir_$next_page((void *)local_handle, depth, extra_array + 2, &page_idx);
        if (page_idx == 0xFFFF) break;  /* No more pages */
        entry_idx = 1;
    }

    at_eof = -1;

finalize:
    if (at_eof < 0) {
        /* At EOF - clear continuation */
        cont[0] = 0;
        cont[1] = 0;
    } else {
        /* More entries available - update continuation */
        cont[1] = page_idx + 1;
        cont[0] = entry_idx;
    }

exit_cleanup:
    /* Compute output sizes */
    *size_ret = word_idx * 2;
    if (last_entry == (uint16_t *)0) {
        *offset_ret = 0;
    } else {
        *offset_ret = (int32_t)((char *)last_entry - (char *)buf_ptr);
    }

    dir_$release_handle(&local_handle);
    ACL_$EXIT_SUPER();
}
