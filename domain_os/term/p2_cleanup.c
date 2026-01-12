#include "term.h"

// External data
extern uid_$t PROC2_UID;      // at 0xe7be94
extern uid_$t UID_$NIL;       // at 0xe1737c - the nil/invalid UID constant
extern char TERM_$DTTE_BASE[];  // at 0xe2c9f0

// DTTE offsets for UID comparison
#define DTTE_UID_HIGH_OFFSET  (0x4dc - 0x338)  // offset within larger structure
#define DTTE_UID_LOW_OFFSET   (0x4dc - 0x334)
#define DTTE_ENTRY_SIZE       0x4dc

// Cleans up terminal state when a process (P2) exits.
//
// Iterates through the DTTE entries (3 of them, indices 0-2) and
// checks if any have a UID matching the exiting process. If so,
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

    as_id = *param1;
    uid_offset = (short)(as_id << 3);  // as_id * 8 for UID array indexing

    // Get pointer to process's UID
    proc_uid = (uid_$t *)((char *)&PROC2_UID + uid_offset);

    // Iterate through 3 DTTE entries (indices 0, 1, 2)
    for (i = 0; i < 3; i++) {
        // Calculate pointer to this DTTE's UID field
        char *dtte_entry = TERM_$DTTE_BASE + DTTE_ENTRY_SIZE + (i * DTTE_ENTRY_SIZE);
        entry_uid = (uid_$t *)(dtte_entry - 0x338);

        // Compare UIDs
        if (entry_uid->high == proc_uid->high &&
            entry_uid->low == proc_uid->low) {
            // Clear this entry's UID to nil
            entry_uid->high = UID_$NIL.high;
            entry_uid->low = UID_$NIL.low;
        }
    }
}
