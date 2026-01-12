#include "term.h"

// Translation tables for converting subsystem-specific status codes
// to canonical status_$t values. Indexed by low word of input status.
extern status_$t TERM_$STATUS_TRANSLATION_TABLE_33[];  // at 0xe2c9dc
extern status_$t TERM_$STATUS_TRANSLATION_TABLE_35[];  // at 0xe2c988
extern status_$t TERM_$STATUS_TRANSLATION_TABLE_36[];  // at 0xe2c9b0

// Subsystem codes found in byte 1 of status_$t
#define SUBSYSTEM_33  0x33
#define SUBSYSTEM_35  0x35
#define SUBSYSTEM_36  0x36

// Converts a subsystem-specific status code to a canonical status_$t.
//
// Status format:
//   byte 0: high byte (typically 0)
//   byte 1: subsystem code (0x33, 0x35, or 0x36)
//   bytes 2-3: index into translation table (low word)
//
// If the subsystem code matches a known value, the status is replaced
// with the corresponding entry from that subsystem's translation table.
// Otherwise, the status is left unchanged.
//
// NOTE: No bounds checking is performed on the index. An out-of-bounds
// index would return whatever data happens to be in memory at that offset,
// resulting in a bogus status code rather than a crash.
//
// TODO: Add bounds checking once table sizes are determined.
void TERM_$STATUS_CONVERT(status_$t *status) {
    unsigned char subsystem;
    unsigned short index;

    // Extract subsystem code from byte 1 (bits 16-23)
    subsystem = (*status >> 16) & 0xFF;

    // Extract index from low word (bits 0-15)
    index = *status & 0xFFFF;

    switch (subsystem) {
        case SUBSYSTEM_33:
            *status = TERM_$STATUS_TRANSLATION_TABLE_33[index];
            break;
        case SUBSYSTEM_35:
            *status = TERM_$STATUS_TRANSLATION_TABLE_35[index];
            break;
        case SUBSYSTEM_36:
            *status = TERM_$STATUS_TRANSLATION_TABLE_36[index];
            break;
        default:
            // Unknown subsystem - leave status unchanged
            break;
    }
}
