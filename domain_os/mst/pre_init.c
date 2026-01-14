/*
 * MST_$PRE_INIT - Early initialization of MST segment configuration
 *
 * This function is called very early during system boot to set up the
 * segment table configuration. On M68020 systems, it overrides the default
 * segment layout values with architecture-specific values.
 *
 * After configuring the segment layout, it initializes the MST_ASID_BASE
 * table which maps each ASID to its starting index in the segment table.
 *
 * Memory Layout (M68020):
 * - Total segments (SEG_TN): 0x680 (1664 segments)
 * - Private A: 0x678 segments (0x000-0x677)
 * - Global A: 0xe0 segments (0x678-0x757)
 * - Private B: 8 segments (0x758-0x75f)
 * - Global B: 0xa0 segments (0x760-0x7ff)
 * - Memory top: 0x800 (segment numbers >= this are invalid)
 */

#include "mst.h"

/* External CPU type flag - high bit set for M68020+ */
extern int8_t M68020;

/*
 * MST_$PRE_INIT - Initialize segment table configuration
 *
 * Called during early boot before full memory management is available.
 * Sets up segment boundaries and initializes the ASID base table.
 */
void MST_$PRE_INIT(void)
{
    int16_t i;
    uint16_t segments_per_asid;

    /*
     * On M68020 systems, override the default segment layout.
     * The high bit of M68020 flag indicates M68020 or later processor.
     */
    if (M68020 < 0) {  /* High bit set = M68020+ */
        MST_$SEG_TN = 0x680;             /* Total segments: 1664 */
        MST_$GLOBAL_A_SIZE = 0xe0;       /* Global A: 224 segments */
        MST_$SEG_GLOBAL_A = 0x678;       /* Global A starts here */
        MST_$SEG_GLOBAL_A_END = 0x757;   /* Global A ends here */
        MST_$PRIVATE_A_SIZE = 0x678;     /* Private A: 1656 segments */
        MST_$SEG_PRIVATE_A_END = 0x677;  /* Private A ends here */
        MST_$SEG_PRIVATE_B = 0x758;      /* Private B starts here */
        MST_$SEG_PRIVATE_B_END = 0x75f;  /* Private B ends here */
        MST_$SEG_PRIVATE_B_OFFSET = 0xe0; /* Offset for Private B in table */
        MST_$SEG_GLOBAL_B = 0x760;       /* Global B starts here */
        MST_$SEG_GLOBAL_B_OFFSET = 0x680; /* Offset for Global B in table */
        MST_$SEG_HIGH = 0x7e0;           /* Highest usable segment */
        MST_$SEG_MEM_TOP = 0x800;        /* Top of address space */
        MST_$GLOBAL_B_SIZE = 0xa0;       /* Global B: 160 segments */
    }

    /*
     * Calculate segments per ASID.
     * Each ASID gets (SEG_TN + 63) / 64 words in the segment table.
     * This rounds up to ensure sufficient space.
     */
    segments_per_asid = (uint16_t)((MST_$SEG_TN + 0x3f) >> 6);

    /*
     * Initialize the ASID base table.
     * Each ASID's base is its index times segments_per_asid.
     * ASID 0 starts at 0, ASID 1 at segments_per_asid, etc.
     */
    for (i = 0; i < MST_MAX_ASIDS; i++) {
        MST_ASID_BASE[i] = (uint16_t)(i * segments_per_asid);
    }
}
