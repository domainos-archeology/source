#include "term/term_internal.h"

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
// The three tables are adjacent in the module data block and their extents
// are fixed by that adjacency (A5 = 0xe2c98c in the original):
//
//   table 35  0xe2c988 .. 0xe2c9b0   10 entries  (A5-0x04, subcodes 0x01-0x09)
//   table 36  0xe2c9b0 .. 0xe2c9dc   11 entries  (A5+0x24, subcodes 0x01-0x0a)
//   table 33  0xe2c9dc .. 0xe2c9f0    5 entries  (A5+0x50, subcodes 0x01-0x04)
//
// Table 36 covers every 0x36 subcode the status database defines (0x01-0x0a).
// Table 35 stops one short of the defined 0x35 range (0x01-0x0b), and table 33
// one short of the defined 0x33 range (0x01-0x05), so those two out-of-range
// subcodes read the first entries of the next table (TERM_$DATA, at 0xe2c9f0,
// follows table 33).
//
// No bounds check is performed: the original indexes the table unconditionally,
// and reproducing that is deliberate.  The index arithmetic is also done in a
// 16-bit register --
//
//   move.w (0x2,A0),D0w ; lsl.w #0x2,D0w ; move.l (0x50,A5,D0w*0x1),(A0)
//
// -- so the scaled offset is truncated to 16 bits and then used as a *signed*
// word displacement.  A low word >= 0x2000 therefore addresses memory below
// the table rather than far above it; the element index below reproduces that.
void TERM_$STATUS_CONVERT(status_$t *status) {
    unsigned char subsystem;
    unsigned short index;
    int elem;

    // Extract subsystem code from byte 1 (bits 16-23)
    subsystem = (*status >> 16) & 0xFF;

    // Extract index from low word (bits 0-15)
    index = *status & 0xFFFF;

    // lsl.w #2 truncates to 16 bits; the index register is then sign-extended
    // by the indexed addressing mode.  The product is always a multiple of 4,
    // so dividing by 4 recovers the signed element index exactly.
    elem = (int)(int16_t)(uint16_t)(index << 2) / 4;

    switch (subsystem) {
        case SUBSYSTEM_33:
            *status = TERM_$STATUS_TRANSLATION_TABLE_33[elem];
            break;
        case SUBSYSTEM_35:
            *status = TERM_$STATUS_TRANSLATION_TABLE_35[elem];
            break;
        case SUBSYSTEM_36:
            *status = TERM_$STATUS_TRANSLATION_TABLE_36[elem];
            break;
        default:
            // Unknown subsystem - leave status unchanged
            break;
    }
}
