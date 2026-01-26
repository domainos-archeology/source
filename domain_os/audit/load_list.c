/*
 * load_list.c - audit_$load_list
 *
 * Loads the audit list from //node_data/audit/audit_list.
 * The list specifies which UIDs should be audited when
 * selective auditing is enabled.
 *
 * Original address: 0x00E7131C
 */

#include "audit/audit_internal.h"
#include "name/name.h"
#include "file/file.h"
#include "mst/mst.h"

/* Path to audit list file */
static const char list_path[] = "//node_data/audit/audit_list";
static int16_t list_path_len = 28;

int8_t audit_$load_list(status_$t *status_ret)
{
    int8_t result = 0;
    uid_t list_uid;
    audit_list_header_t *header;
    uint32_t mapped_size;
    uid_t *uid_array;
    int16_t i;
    uint16_t lock_index = 0;
    uint16_t lock_mode = 0;
    uint8_t rights = 0;

    /* Try to resolve the list file */
    NAME_$RESOLVE((char *)list_path, &list_path_len, &list_uid, status_ret);

    if (*status_ret == status_$naming_name_not_found) {
        /* No audit list file - selective auditing disabled */
        *status_ret = status_$ok;
        return result;
    }

    if (*status_ret != status_$ok) {
        return result;
    }

    /* Lock the file for reading */
    FILE_$LOCK(&list_uid, &lock_index, &lock_mode, &rights, 0, status_ret);

    if (*status_ret != status_$ok) {
        return result;
    }

    /* Map the file for reading */
    header = (audit_list_header_t *)MST_$MAPS(
        PROC1_$AS_ID,           /* asid */
        (int8_t)-1,             /* flags */
        &list_uid,
        0,                      /* offset */
        AUDIT_BUFFER_MAP_SIZE,
        0x16,                   /* protection */
        0,                      /* unused */
        (int8_t)-1,             /* writable (actually read-only) */
        &mapped_size,
        status_ret
    );

    if (*status_ret != status_$ok) {
        FILE_$UNLOCK(&list_uid, &lock_mode, status_ret);
        return result;
    }

    /* Validate file format version */
    if (header->version > AUDIT_LIST_VERSION_MAX) {
        *status_ret = status_$audit_event_list_not_current_format;
        goto unmap;
    }

    /* Validate entry count */
    if (header->entry_count > AUDIT_MAX_LIST_ENTRIES) {
        *status_ret = status_$audit_excessive_event_types;
        goto unmap;
    }

    /* Clear existing hash table */
    ML_$EXCLUSION_START((ml_$exclusion_t *)((char *)AUDIT_$DATA.event_count + 0x0C));

    audit_$clear_hash_table();

    /* Copy header information */
    AUDIT_$DATA.flags = header->flags;
    AUDIT_$DATA.list_uid.high = header->list_uid.high;
    AUDIT_$DATA.list_uid.low = header->list_uid.low;
    AUDIT_$DATA.list_count = header->entry_count;
    AUDIT_$DATA.timeout = header->timeout_units * 4;  /* Convert to 4-second units */

    /* Add each UID to the hash table */
    uid_array = (uid_t *)(header + 1);  /* UIDs follow header */

    for (i = 0; i < AUDIT_$DATA.list_count; i++) {
        audit_$add_to_hash(&uid_array[i], status_ret);
        if (*status_ret != status_$ok) {
            break;
        }
    }

    ML_$EXCLUSION_STOP((ml_$exclusion_t *)((char *)AUDIT_$DATA.event_count + 0x0C));

    /* Signal that list was updated */
    EC_$ADVANCE(AUDIT_$DATA.event_count);

    result = (int8_t)-1;  /* Success */

unmap:
    /* Unmap the file */
    MST_$UNMAP_PRIVI(1, &UID_$NIL, (uint32_t)(uintptr_t)header, mapped_size, 0, status_ret);

    /* Unlock the file */
    FILE_$UNLOCK(&list_uid, &lock_mode, status_ret);

    return result;
}
