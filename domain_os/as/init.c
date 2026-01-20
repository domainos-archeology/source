/*
 * AS_$INIT - Initialize address space configuration
 *
 * Reverse engineered from Domain/OS at address 0x00E3151C
 *
 * This function adjusts the address space layout for M68020 systems.
 * On M68010 systems, the default values are used unchanged.
 * On M68020 systems, the address space is expanded:
 * - Global A base moves to 0x33C0000
 * - Global A size is 0x700000 bytes
 * - Stack and CR record addresses are offset by 0x2A00000
 *
 * The M68020 flag is checked by testing if its high bit is set
 * (i.e., the signed byte value is negative).
 */

#include "as/as.h"
#include "mmu/mmu.h"

/*
 * M68020 address space adjustment offset
 * Applied to stack and CR record addresses on M68020 systems
 */
#define M68020_AS_OFFSET  0x2A00000

/*
 * M68020 Global A configuration
 */
#define M68020_GLOBAL_A_BASE  0x33C0000
#define M68020_GLOBAL_A_SIZE  0x700000

void AS_$INIT(void)
{
    /*
     * Check if this is an M68020 system by testing the high bit
     * of the M68020 flag byte. If negative (bit 7 set), apply
     * M68020-specific adjustments.
     *
     * Assembly: tst.b (0x00e23d2e).l; bpl.b skip
     */
    if ((int8_t)M68020 < 0) {
        /*
         * Update Global A region for M68020 address space
         * Assembly:
         *   move.l #0x33c0000,(0x4,A0)   ; global_a = 0x33C0000
         *   move.l #0x700000,(0x8,A0)    ; global_a_size = 0x700000
         */
        AS_$INFO.global_a = M68020_GLOBAL_A_BASE;
        AS_$INFO.global_a_size = M68020_GLOBAL_A_SIZE;

        /*
         * Copy to M68020-specific fields
         * Assembly:
         *   lea (0x4,A0),A1
         *   move.l (A1)+,(0xc,A0)    ; m68020_global_a
         *   move.l (A1)+,(0x10,A0)   ; m68020_global_a_size
         */
        AS_$INFO.m68020_global_a = AS_$INFO.global_a;
        AS_$INFO.m68020_global_a_size = AS_$INFO.global_a_size;

        /*
         * Apply offset to stack and CR record addresses
         * Assembly: add.l D0,(offset,A0) where D0 = 0x2A00000
         */
        AS_$INFO.stack_file_low += M68020_AS_OFFSET;  /* +0x18 */
        AS_$INFO.cr_rec += M68020_AS_OFFSET;          /* +0x1C */
        AS_$INFO.cr_rec_end += M68020_AS_OFFSET;      /* +0x24 */
        AS_$INFO.stack_file_high += M68020_AS_OFFSET; /* +0x2C */
        AS_$INFO.stack_low += M68020_AS_OFFSET;       /* +0x34 */
        AS_$INFO.stack_high += M68020_AS_OFFSET;      /* +0x3C */
        AS_$INFO.stack_offset += M68020_AS_OFFSET;    /* +0x44 */

        /*
         * Set CR record file address to CR record end
         * Assembly: move.l (0x24,A0),(0x50,A0)
         */
        AS_$INFO.cr_rec_file = AS_$INFO.cr_rec_end;
    }
}
