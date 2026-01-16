/*
 * PROC2_$SET_VALID - Mark process as valid
 *
 * Called during process startup to mark the current process as valid.
 * Performs the following:
 * 1. If stack UID is nil, maps the stack area
 * 2. Sets the valid bit (0x80) in process flags
 * 3. If process has ORPHAN flag but not ALT_ASID flag, initializes
 *    various fields in the creation record
 *
 * Original address: 0x00e73484
 */

#include "proc2/proc2_internal.h"

/*
 * Creation record structure (partial - offsets determined from decompilation)
 * This structure is pointed to by proc2_info_t.cr_rec and contains
 * process creation and accounting information.
 */
typedef struct cr_rec_t {
    uint32_t    fields_0x00[0x1d];  /* 0x00-0x73: Various fields */
    uint32_t    field_74;           /* 0x74: CPU time related */
    uint16_t    field_78;           /* 0x78: CPU time related */
    uint32_t    field_7c;           /* 0x7C: Timing */
    uint32_t    field_80;           /* 0x80: Timing */
    uint32_t    field_84;           /* 0x84: Timing */
    uint8_t     field_88;           /* 0x88: Flag byte */
    uint8_t     field_89;           /* 0x89: Flag byte */
    uint32_t    field_8a;           /* 0x8A: CPU usage */
    uint16_t    field_8e;           /* 0x8E: CPU usage */
    uint8_t     field_90;           /* 0x90: Flag */
    uint8_t     pad_91[3];          /* 0x91: Padding */
    status_$t   status;             /* 0x94: Status */
    uid_t      proc_uid;           /* 0x98: Process UID */
    uid_t      parent_uid;         /* 0xA0: Parent UID */
    uid_t      stack_uid;          /* 0xA8: Stack file UID */
    uint32_t    addr_lo;            /* 0xB0: Stack address low */
    uint32_t    size;               /* 0xB4: Stack size */
    uint32_t    field_b8;           /* 0xB8: Parent upid */
    uid_t      debugger_uid;       /* 0xBC: Debugger UID */
    uint8_t     pad_c4;             /* 0xC4: Padding */
    uint8_t     flags_c5;           /* 0xC5: Flags byte */
    uint16_t    count_c6;           /* 0xC6: Counter (set to 1) */
    uint8_t     field_c8;           /* 0xC8: Field */
} cr_rec_t;

void PROC2_$SET_VALID(void)
{
    int16_t current_idx;
    proc2_info_t *entry;
    cr_rec_t *cr_rec;
    uid_t *stack_uid_ptr;
    status_$t status;

    /* Get current process's table entry */
    current_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);
    entry = P2_INFO_ENTRY(current_idx);

    /* Get creation record pointer */
    cr_rec = (cr_rec_t *)entry->cr_rec;

    /* Get stack UID pointer (at offset in proc2 table after cr_rec_2) */
    stack_uid_ptr = (uid_t *)((char *)entry + 0xDC);  /* Approximate offset */

    /*
     * If stack UID is nil, need to map the stack area
     */
    if (stack_uid_ptr->high == UID_$NIL.high && stack_uid_ptr->low == UID_$NIL.low) {
        /* Initialize stack mapping parameters in creation record */
        cr_rec->addr_lo = AS_$STACK_FILE_LOW;
        cr_rec->size = AS_$INIT_STACK_FILE_SIZE.value;

        /* Map the stack area */
        MST_$MAP_AREA_AT(&cr_rec->addr_lo, &cr_rec->size,
                         NULL, NULL,  /* Two parameters from PC-relative data */
                         stack_uid_ptr, &cr_rec->status);

        /* If mapping failed, delete the process */
        if (cr_rec->status != status_$ok) {
            PROC2_$DELETE();
            /* Does not return */
        }

        /* Copy stack UID to creation record */
        cr_rec->stack_uid.high = stack_uid_ptr->high;
        cr_rec->stack_uid.low = stack_uid_ptr->low;
    }

    /* Set the valid bit under lock */
    ML_$LOCK(PROC2_LOCK_ID);
    entry->flags |= 0x0080;  /* Set valid bit */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /*
     * If ORPHAN flag is set but not ALT_ASID flag, initialize creation record
     * This happens for newly forked processes that need to set up their
     * creation record with parent process information.
     */
    if ((entry->flags & PROC2_FLAG_ORPHAN) != 0 &&
        (entry->flags & PROC2_FLAG_ALT_ASID) == 0) {

        /* Set process UID from PROC2_UID table using PROC1_$AS_ID */
        uid_t *uid_table = &PROC2_UID;
        int uid_offset = PROC1_$AS_ID * 8;
        cr_rec->proc_uid.high = *(uint32_t *)((char *)uid_table + uid_offset);
        cr_rec->proc_uid.low = *(uint32_t *)((char *)uid_table + uid_offset + 4);

        /* Set parent upid */
        cr_rec->field_b8 = entry->upid;

        /* Copy parent UID from entry */
        /* TODO: entry offset for parent_uid needs verification */

        /* Copy stack UID */
        cr_rec->stack_uid.high = stack_uid_ptr->high;
        cr_rec->stack_uid.low = stack_uid_ptr->low;

        /* Clear various fields */
        cr_rec->field_74 = 0;
        cr_rec->field_78 = 0;
        cr_rec->field_7c = 0;
        cr_rec->field_80 = 0;
        cr_rec->field_84 = 0;
        cr_rec->field_90 = 0;
        cr_rec->field_c8 = 0;
        cr_rec->count_c6 = 1;

        /* Set debugger UID if debug flag set */
        if ((cr_rec->flags_c5 & 0x08) != 0) {
            int debugger_offset = entry->debugger_idx * 8;
            cr_rec->debugger_uid.high = *(uint32_t *)((char *)uid_table + debugger_offset);
            cr_rec->debugger_uid.low = *(uint32_t *)((char *)uid_table + debugger_offset + 4);
        }

        /* Set flag based on session_id being non-zero */
        cr_rec->field_90 = (entry->session_id != 0) ? 0xFF : 0x00;

        /* Clear first 14 longwords of creation record */
        uint32_t *ptr = (uint32_t *)cr_rec;
        for (int i = 0; i < 14; i++) {
            ptr[i] = 0;
        }

        /* Clear longwords at offset 0x3C-0x70 (14 more longwords) */
        ptr = (uint32_t *)((char *)cr_rec + 0x3C);
        for (int i = 0; i < 14; i++) {
            ptr[i] = 0;
        }
    }
}
