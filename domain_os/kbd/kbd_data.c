/*
 * kbd/kbd_data.c - KBD / MNK global data definitions
 *
 * Original M68K addresses:
 *   MNK_$KTT_PTRS    0x00E273DC  0x20 bytes (8 pointers)
 *   MNK_$KTT_MAX     0x00E273FC  2 bytes
 *   KBD_$MODE_TABLE  0x00E2DDE4  8 bytes
 *   kbd_$escape_state 0x00E2DDEC 0x10 bytes (8 words)
 *   kbd_$state_range 0x00E2DDFC  0x40 bytes (32 words)
 *   kbd_$state_table 0x00E2DEA4  0x14 bytes (10 entries)
 */

#include "kbd/kbd_internal.h"

/*
 * MNK_$KTT_PTRS - keyboard translation table pointers, one per keyboard type.
 *
 * The map has MNK_$KTT_PTRS at 0x00E273DC and MNK_$KTT_MAX at 0x00E273FC, so
 * the table is 0x20 bytes = 8 longwords; KBD_$SET_TYPE indexes it after
 * rejecting anything above MNK_$KTT_MAX (7), which is the same 8 entries.
 * Image longwords: 00E273FE, 00E8497E, 00E84A7E, 00E84B7E, 00E84B7E,
 * 00E84C7E, 00E273FE, 00E84D7E -- i.e. the KTTs the map names.
 */
void *MNK_$KTT_PTRS[8] = {
    SMD_$KTT,          /* 0: 0x00E273FE - US */
    SMD_$GERMAN_KTT,   /* 1: 0x00E8497E */
    SMD_$FRENCH_KTT,   /* 2: 0x00E84A7E */
    SMD_$SWEDISH_KTT,  /* 3: 0x00E84B7E (= SMD_$NORWEGIAN_KTT) */
    SMD_$SWEDISH_KTT,  /* 4: 0x00E84B7E, the same table again */
    SMD_$UK_KTT,       /* 5: 0x00E84C7E */
    SMD_$KTT,          /* 6: 0x00E273FE - US again */
    SMD_$SWISS_KTT,    /* 7: 0x00E84D7E */
};
#if defined(ARCH_M68K)
_Static_assert(sizeof(MNK_$KTT_PTRS) == 0x20,
               "MNK_$KTT_PTRS: 0x00E273DC..0x00E273FC (MNK_$KTT_MAX)");
#endif

/*
 * MNK_$KTT_MAX - highest valid keyboard type index, 0x00E273FC.
 * Two bytes: SMD_$KTT starts at 0x00E273FE.  Image word: 0x0007.
 */
int16_t MNK_$KTT_MAX = 7;

/*
 * KBD_$MODE_TABLE - keyboard mode translation table, 0x00E2DDE4, the first
 * object of the map segment "D E2DDE4 KBD size = D4".  Eight bytes: the next
 * object, kbd_$escape_state, starts at 0x00E2DDEC.  KBD_$GET_CHAR_AND_MODE indexes
 * it with the mode kbd_$fetch_key returns.
 * Image bytes: 00 01 02 03 12 10 11 0F.
 */
uint8_t KBD_$MODE_TABLE[8] = {
    0x00, 0x01, 0x02, 0x03, 0x12, 0x10, 0x11, 0x0f
};

/*
 * kbd_$escape_state - 0x00E2DDEC (A5+0x08), eight words.
 *
 * The escape state each keyboard type moves to when the received byte's low
 * nibble is 0x0F.  Both readers index it with a word scale off the KBD
 * module's A5 = 0x00E2DDE4 (`lea (0xe2dde4).l,A5` at 0x00E1CB06 and
 * 0x00E1CCC8):
 *   0x00E1CE2A  KBD_$RCV        move.w (0x3c,A2),D1w / add.w D1w,D1w /
 *                               move.w (0x8,A5,D1w),(0x38,A2)
 *                               i.e. state->state = table[state->kbd_type_idx]
 *   0x00E1CBD6  kbd_$fetch_key  move.w (A0),D1w / add.w D1w,D1w /
 *                               move.w (0x8,A5,D1w),D4w, D4 -> state->sub_state
 * Both indices run 0..MNK_$KTT_MAX (7), the same range KBD_$MODE_TABLE and
 * MNK_$KTT_PTRS use, so the table is eight words and ends at 0x00E2DDFC.
 */
uint16_t kbd_$escape_state[8] = {
    0x0000, 0x0008, 0x0006, 0x0007, 0x000e, 0x000e, 0x000e, 0x000e
};
#if defined(ARCH_M68K)
_Static_assert(sizeof(kbd_$escape_state) == 0x10,
               "kbd_$escape_state: 0x00E2DDEC..0x00E2DDFC");
#endif

/*
 * kbd_$state_range - 0x00E2DDFC (A5+0x18), 32 words, up to TERM_$TPAD_BUFFER
 * (0x00E2DE3C): sixteen (first, limit) pairs, one per state.
 *
 * kbd_$state_lookup (0x00E1C9FC) reads the pair for `state` with
 * `lsl.w #2` / (0x18,A5,D1w) and (0x1a,A5,D1w), scans
 * kbd_$state_table[first .. limit-1] for the key and otherwise returns
 * &kbd_$state_table[limit] (the state's default entry).  (An earlier note
 * here said nothing read this table; the lookup does, through the KBD A5
 * its two callers load.)  The last pair (0, 0) belongs to no reachable
 * state: next-state nibbles are 0..0xE, 0xF meaning kbd_$escape_state.
 */
uint16_t kbd_$state_range[32] = {
    0x0000, 0x0005, 0x0006, 0x0006, 0x0007, 0x0007, 0x0008, 0x0008,
    0x0009, 0x0009, 0x000a, 0x000f, 0x0010, 0x0011, 0x0012, 0x0013,
    0x0014, 0x0016, 0x0017, 0x001a, 0x001b, 0x001c, 0x001d, 0x001d,
    0x001e, 0x001f, 0x0020, 0x0020, 0x0021, 0x0023, 0x0000, 0x0000
};
#if defined(ARCH_M68K)
_Static_assert(sizeof(kbd_$state_range) == 0x40,
               "kbd_$state_range: 0x00E2DDFC..0x00E2DE3C (TERM_$TPAD_BUFFER)");
#endif

/*
 * kbd_$state_table - 0x00E2DEA4 (A5+0xC0), after TERM_$TPAD_BUFFER and four
 * zero bytes; the last 0x14 bytes of the KBD segment (0x00E2DEB8 = SIO_IO).
 * Image bytes: FB 20 E8 31 DF 31 FE B4 FF 10 00 10 00 42 00 53 00 6F 00 C0.
 * Entry = (key, action<<4 | next state):
 *   state 0, [0..4] keys FB E8 DF FE FF, default [5] (00,10) action 1
 *   state 1 default [6] (00,42); 2 [7] (00,53); 3 [8] (00,6F); 4 [9] (00,C0)
 * kbd_$state_range gives states 5..14 entries 10..35, which lie past the
 * segment, in SIO2681_$DATA's bytes (0x00E2DEB8..0x00E2DEEC); this array
 * stops at the segment end.  TODO(source-kz1g).
 */
kbd_$state_entry_t kbd_$state_table[KBD_STATE_TABLE_ENTRIES] = {
    { 0xfb, 0x20 }, { 0xe8, 0x31 }, { 0xdf, 0x31 }, { 0xfe, 0xb4 },
    { 0xff, 0x10 }, { 0x00, 0x10 }, { 0x00, 0x42 }, { 0x00, 0x53 },
    { 0x00, 0x6f }, { 0x00, 0xc0 }
};
_Static_assert(sizeof(kbd_$state_table) == 0x14,
               "kbd_$state_table: 0x00E2DEA4..0x00E2DEB8 (SIO_IO)");
