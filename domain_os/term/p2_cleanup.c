#include "term.h"

// External data
extern uid_$t PROC2_UID;      // at 0xe7be94
extern uid_$t UID_$NIL;       // at 0xe1737c - the nil/invalid UID constant

// Per-line terminal data uses 0x4dc byte entries with UID at offset 0x1a4
// These overlap with TERM_$DATA starting at offset 0
#define TERM_LINE_DATA_SIZE   0x4dc
#define TERM_LINE_UID_OFFSET  0x1a4

// Cleans up terminal state when a process (P2) exits.
//
// Iterates through the terminal line data entries (3 of them, indices 0-2)
// and checks if any have a UID matching the exiting process. If so,
// that entry's UID is cleared to UID_$NIL.
//
// The parameter points to the process/address space ID used to
// compute the UID lookup offset.
void TERM_$P2_CLEANUP(short *param1) {
    short as_id;
    int i;
    int uid_offset;
    uid_$t *entry_uid;
    uid_$t *proc_uid;
    char *term_base = (char *)&TERM_$DATA;

    as_id = *param1;
    uid_offset = (short)(as_id << 3);  // as_id * 8 for UID array indexing

    // Get pointer to process's UID
    proc_uid = (uid_$t *)((char *)&PROC2_UID + uid_offset);

    // Iterate through 3 terminal line entries (indices 0, 1, 2)
    // Each entry is TERM_LINE_DATA_SIZE (0x4dc) bytes with UID at TERM_LINE_UID_OFFSET (0x1a4)
    for (i = 0; i < 3; i++) {
        // Calculate pointer to this line's UID field
        entry_uid = (uid_$t *)(term_base + (i * TERM_LINE_DATA_SIZE) + TERM_LINE_UID_OFFSET);

        // Compare UIDs
        if (entry_uid->high == proc_uid->high &&
            entry_uid->low == proc_uid->low) {
            // Clear this entry's UID to nil
            entry_uid->high = UID_$NIL.high;
            entry_uid->low = UID_$NIL.low;
        }
    }
}
