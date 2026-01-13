// OSINFO_$GET_SEG_TABLE - Get segment table entries
// Address: 0x00e5c5c4
// Size: 208 bytes
//
// Copies segment table entries (AOTE or AST) to the caller's buffer.
// Table type 1 = AOTE (0x80 bytes per entry)
// Table type 2 = AST (0x14 bytes per entry)

#include "osinfo.h"

// External segment table data
extern short AST_$SIZE_AST;           // Number of AST entries

// AOTE table at 0xed5000 (0x80 bytes per entry)
extern uint32_t AOTE_TABLE[];

// AST table at 0xec5400 (0x14 bytes per entry)
extern uint32_t AST_TABLE[];

void OSINFO_$GET_SEG_TABLE(short *type_ptr, void *buffer,
                           short *max_entries_ptr, short *actual_entries_ptr,
                           short *total_entries_ptr, status_$t *status)
{
    short table_type;
    short max_entries;
    short entries_to_copy;
    short i, j;
    uint32_t *src;
    uint32_t *dst;

    *status = status_$ok;

    // Return total entries available
    *total_entries_ptr = AST_$SIZE_AST;

    max_entries = *max_entries_ptr;
    if (max_entries == 0) {
        return;  // Nothing to copy
    }

    // Determine how many entries to copy
    if (max_entries < AST_$SIZE_AST) {
        entries_to_copy = max_entries;
        *status = status_$os_info_array_too_small;
    } else {
        entries_to_copy = AST_$SIZE_AST;
    }
    *actual_entries_ptr = entries_to_copy;

    table_type = *type_ptr;

    if (table_type == SEG_TABLE_TYPE_AST) {
        // Copy AST entries (0x14 bytes = 5 longs each)
        dst = (uint32_t *)buffer;
        src = AST_TABLE;
        for (i = 0; i < entries_to_copy; i++) {
            for (j = 0; j < 5; j++) {
                *dst++ = *src++;
            }
        }
    } else if (table_type == SEG_TABLE_TYPE_AOTE) {
        // Copy AOTE entries (0x80 bytes = 32 longs each)
        dst = (uint32_t *)buffer;
        src = AOTE_TABLE;
        for (i = 0; i < entries_to_copy; i++) {
            for (j = 0; j < 32; j++) {
                *dst++ = *src++;
            }
        }
    }
    // If table_type is neither 1 nor 2, nothing is copied
}
