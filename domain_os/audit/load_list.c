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

/*
 * The constant cells at 0x00E71494-0x00E714B4, with their image bytes:
 *
 *   00e71494  00 1b               the path length, 27
 *   00e71496  00 01               FILE_$LOCK's lock_index AND lock_mode, and
 *                                 FILE_$UNLOCK's lock_mode - one shared cell
 *                                 (`pea (0x132,PC)` + `move.l (SP),-(SP)` at
 *                                 0x00E71362, and `pea (0x1a,PC)` at
 *                                 0x00E7147A)
 *   00e71498  00 00               FILE_$LOCK's rights cell
 *   00e7149a  60 6e 6f 64 65 ...  "`node_data/audit/audit_list"
 *
 * The path starts with a BACKQUOTE, Domain/OS' shorthand for the calling
 * node's own //node_data, not with "//".  27 characters, no terminator.
 */
static const char list_path[] = "`node_data/audit/audit_list";   /* 0x00E7149A */
static const int16_t list_path_len = 0x001B;                     /* 0x00E71494 */
static const int16_t list_lock_one  = 0x0001;                    /* 0x00E71496 */
static const int16_t list_lock_zero = 0x0000;                    /* 0x00E71498 */

int8_t audit_$load_list(status_$t *status_ret)
{
    int8_t result = 0;
    uid_t list_uid;
    audit_list_header_t *header;
    uint32_t mapped_size;
    uid_t *uid_array;
    int16_t i;
    /* A6-0x18: FILE_$LOCK's 8-byte lock_info output (0x00E7135A) */
    uint8_t lock_info[8];

    /* 0x00E71326-0x00E7133C */
    NAME_$RESOLVE((char *)list_path, (int16_t *)&list_path_len,
                  &list_uid, status_ret);

    if (*status_ret == status_$naming_name_not_found) {
        /* No audit list file - selective auditing disabled */
        *status_ret = status_$ok;
        return result;
    }

    if (*status_ret != status_$ok) {
        return result;
    }

    /*
     * 0x00E71358-0x00E71372.  lock_index and lock_mode are the SAME cell -
     * the compiler pushed 0x00E71496 once and duplicated the stack slot with
     * `move.l (SP),-(SP)` - and rights is the separate zero cell.
     */
    FILE_$LOCK(&list_uid,
               (const uint16_t *)&list_lock_one,
               (const uint16_t *)&list_lock_one,
               (const uint8_t *)&list_lock_zero,
               lock_info, status_ret);

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
        /* 0x00E713B4 branches straight to the unlock at 0x00E71476, past
         * the unmap. */
        FILE_$UNLOCK(&list_uid, (uint16_t *)&list_lock_one, status_ret);
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

    /* 0x00E71476-0x00E71486: the mode cell is the same 0x00E71496 = 1. */
    FILE_$UNLOCK(&list_uid, (uint16_t *)&list_lock_one, status_ret);

    return result;
}
