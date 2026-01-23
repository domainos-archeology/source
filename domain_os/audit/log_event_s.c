/*
 * log_event_s.c - AUDIT_$LOG_EVENT_S
 *
 * Core event logging function that writes audit records to the log file.
 *
 * Original address: 0x00E70E40
 *
 * Event record format:
 *   0x00: uint16_t record_size    - Total record size
 *   0x02: uint16_t version        - Record version (1)
 *   0x04: uint8_t[36] sid_data    - SID data
 *   0x28: uint16_t event_flags    - Event flags
 *   0x2A: uint32_t node_id        - Node ID (upper 20 bits)
 *   0x2E: uid_t event_uid         - Event UID
 *   0x36: uint32_t status         - Event status
 *   0x3A: clock_t timestamp       - Event timestamp
 *   0x40: int16_t process_id      - Level 1 process ID
 *   0x42: int16_t upid_high       - UPID high word
 *   0x44: int16_t upid_low        - UPID low word
 *   0x46: char[] data             - Variable-length data (null-terminated)
 */

#include "audit/audit_internal.h"
#include "acl/acl.h"
#include "time/time.h"
#include "os/os.h"
#include "file/file.h"
#include "mst/mst.h"

/* External declarations */
extern int32_t NODE_$ME;

void AUDIT_$LOG_EVENT_S(uid_t *event_uid, uint16_t *event_flags,
                        void *sid, uint32_t *status,
                        char *data, uint16_t *data_len)
{
    int16_t pid;
    uint16_t actual_len;
    uint16_t record_size;
    audit_event_record_t *record;
    audit_hash_node_t *node;
    int16_t bucket;
    int8_t should_log;
    status_$t local_status;

    /* Only proceed if auditing is enabled */
    if (AUDIT_$ENABLED >= 0) {
        return;
    }

    pid = PROC1_$CURRENT;

    /* Check if process is suspended */
    if (AUDIT_$DATA.suspend_count[pid] != 0) {
        return;
    }

    /* Suspend auditing for this process while logging */
    AUDIT_$DATA.suspend_count[pid]++;

    /* Clamp data length to maximum */
    actual_len = *data_len;
    if (actual_len > AUDIT_MAX_DATA_SIZE) {
        actual_len = AUDIT_MAX_DATA_SIZE;
    }

    local_status = status_$ok;

    /* Lock the exclusion */
    ML_$EXCLUSION_START((ml_$exclusion_t *)((char *)AUDIT_$DATA.event_count + 0x0C));

    /* Check if we should log this event */
    should_log = AUDIT_$CORRUPTED;

    if ((AUDIT_$DATA.flags & AUDIT_FLAG_SELECTIVE) != 0) {
        should_log = (int8_t)-1;  /* Corrupted means log everything */
    }

    if (should_log >= 0) {
        /* Check if selective auditing is active */
        if (AUDIT_$DATA.list_count != 0) {
            /* Check if event UID is in the audit list */
            if (should_log >= 0) {
                bucket = UID_$HASH(event_uid, &AUDIT_HASH_MODULO);

                for (node = AUDIT_$DATA.hash_buckets[bucket]; node != NULL; node = node->next) {
                    if (node->uid_high == event_uid->high &&
                        node->uid_low == event_uid->low) {
                        break;
                    }
                }

                if (node == NULL) {
                    /* UID not in list, skip logging */
                    goto done;
                }
            }
            /* Fall through to log */
        } else {
            /* No audit list, skip logging */
            goto done;
        }
    }

    /* Calculate record size (header + data + null terminator, rounded up to even) */
    record_size = 0x47 + actual_len;
    if ((record_size & 1) != 0) {
        record_size++;  /* Round up to even */
    }

    /* Check if we have enough space in the buffer */
    if ((int32_t)record_size > (int32_t)AUDIT_$DATA.bytes_remaining) {
        /* Need to remap the buffer - flush current and extend file */
        AUDIT_$DATA.file_offset += (uintptr_t)AUDIT_$DATA.write_ptr -
                                   (uintptr_t)AUDIT_$DATA.buffer_base;

        ACL_$ENTER_SUPER();

        AUDIT_$DATA.dirty = 0;

        FILE_$FW_FILE(&AUDIT_$DATA.log_file_uid, &local_status);

        if (local_status == status_$ok) {
            /* Unmap current buffer */
            MST_$UNMAP_PRIVI(1, &UID_$NIL,
                             (uint32_t)(uintptr_t)AUDIT_$DATA.buffer_base,
                             AUDIT_$DATA.buffer_size,
                             0, &local_status);
        }

        if (local_status == status_$ok) {
            /* Map new buffer at new offset */
            AUDIT_$DATA.buffer_base = MST_$MAPS_RET(
                0,              /* asid */
                (int8_t)-1,     /* flags */
                &AUDIT_$DATA.log_file_uid,
                AUDIT_$DATA.file_offset,
                AUDIT_BUFFER_MAP_SIZE,
                0x16,           /* protection */
                0,              /* unused */
                (int8_t)-1,     /* writable */
                &AUDIT_$DATA.buffer_size,
                &local_status
            );
        }

        ACL_$EXIT_SUPER();

        if (local_status != status_$ok) {
            goto error;
        }

        AUDIT_$DATA.write_ptr = AUDIT_$DATA.buffer_base;
        AUDIT_$DATA.bytes_remaining = AUDIT_$DATA.buffer_size;
    }

    /* Write the record */
    record = (audit_event_record_t *)AUDIT_$DATA.write_ptr;

    record->record_size = record_size;
    record->version = 1;

    /* Copy SID data (36 bytes = 9 uint32_t values) */
    {
        uint32_t *src = (uint32_t *)sid;
        uint32_t *dst = (uint32_t *)record->sid_data;
        int i;
        for (i = 0; i < 9; i++) {
            dst[i] = src[i];
        }
    }

    record->event_flags = *event_flags;

    /* Set node ID (mask off lower 12 bits, shift upper bits) */
    record->node_id = (record->node_id & 0xFFF) | (NODE_$ME << 12);

    /* Copy event UID */
    record->event_uid.high = event_uid->high;
    record->event_uid.low = event_uid->low;

    /* Copy status */
    record->status = *status;

    /* Get timestamp */
    TIME_$CLOCK(&record->timestamp);

    /* Get process IDs */
    if (PROC1_$AS_ID == 0) {
        record->process_id = PROC1_$CURRENT;
        record->upid_high = 0;
        record->upid_low = 0;
    } else {
        PROC2_$GET_MY_UPIDS(&record->upid_high, &record->upid_low, &record->process_id);
    }

    /* Copy data */
    OS_$DATA_COPY(data, (char *)record + 0x46, (int32_t)actual_len);

    /* Null-terminate */
    *((char *)record + 0x46 + actual_len) = '\0';

    /* Advance write pointer */
    AUDIT_$DATA.write_ptr = (char *)AUDIT_$DATA.write_ptr + record_size;
    AUDIT_$DATA.bytes_remaining -= record_size;
    AUDIT_$DATA.dirty = (uint8_t)-1;

done:
    if (local_status != status_$ok) {
error:
        /* Error occurred, try to recover */
        ACL_$ENTER_SUPER();
        audit_$close_log(&local_status);
        audit_$open_log(&local_status);
        ACL_$EXIT_SUPER();

        if (local_status != status_$ok) {
            CRASH_SYSTEM(&local_status);
        }
    }

    /* Unlock the exclusion */
    ML_$EXCLUSION_STOP((ml_$exclusion_t *)((char *)AUDIT_$DATA.event_count + 0x0C));

    /* Resume auditing for this process */
    AUDIT_$DATA.suspend_count[PROC1_$CURRENT]--;
}
