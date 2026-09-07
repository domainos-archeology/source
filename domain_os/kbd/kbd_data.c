/*
 * kbd/kbd_data.c - KBD / MNK global data definitions
 *
 * Original M68K addresses:
 *   MNK_$KTT_PTRS    0x00E273DC  0x20 bytes (8 pointers)
 *   MNK_$KTT_MAX     0x00E273FC  2 bytes
 *   KBD_$MODE_TABLE  0x00E2DDE4  8 bytes
 *   DAT_00e2ddec     0x00E2DDEC  0x50 bytes (40 words)
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
 * object, DAT_00e2ddec, starts at 0x00E2DDEC.  KBD_$GET_CHAR_AND_MODE indexes
 * it with the mode kbd_$fetch_key returns.
 * Image bytes: 00 01 02 03 12 10 11 0F.
 */
uint8_t KBD_$MODE_TABLE[8] = {
    0x00, 0x01, 0x02, 0x03, 0x12, 0x10, 0x11, 0x0f
};

/*
 * DAT_00e2ddec - 0x00E2DDEC, the word table that follows KBD_$MODE_TABLE and
 * runs up to TERM_$TPAD_BUFFER (0x00E2DE3C), i.e. 0x50 bytes = 40 words.
 *
 * KBD_$RCV reads only the first eight of them, indexed by the descriptor's
 * kbd_type_idx (the same 0..MNK_$KTT_MAX range MNK_$KTT_PTRS uses): the state
 * KBD_$RCV moves to when the received byte's low nibble is 0x0F.  The
 * remaining 32 words are part of the same unnamed KBD block; they are carried
 * here so the object covers the image bytes exactly.
 *
 * TODO(source-wk2f, 0x00E2DDFC): nothing in the tree reaches words 8..39 yet,
 * so their purpose is unrecovered.
 */
uint16_t DAT_00e2ddec[40] = {
    /* words 0..7: KBD_$RCV's per-keyboard-type "escape" state */
    0x0000, 0x0008, 0x0006, 0x0007, 0x000e, 0x000e, 0x000e, 0x000e,
    /* words 8..39: 0x00E2DDFC..0x00E2DE3B */
    0x0000, 0x0005, 0x0006, 0x0006, 0x0007, 0x0007, 0x0008, 0x0008,
    0x0009, 0x0009, 0x000a, 0x000f, 0x0010, 0x0011, 0x0012, 0x0013,
    0x0014, 0x0016, 0x0017, 0x001a, 0x001b, 0x001c, 0x001d, 0x001d,
    0x001e, 0x001f, 0x0020, 0x0020, 0x0021, 0x0023, 0x0000, 0x0000,
};
#if defined(ARCH_M68K)
_Static_assert(sizeof(DAT_00e2ddec) == 0x50,
               "DAT_00e2ddec: 0x00E2DDEC..0x00E2DE3C (TERM_$TPAD_BUFFER)");
#endif
