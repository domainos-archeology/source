/*
 * MST_$VA_TO_SEGNO - Convert virtual address to segment table index
 *
 * This function translates a virtual address into a segment table index,
 * handling the complex mapping between virtual segment numbers and their
 * location in the segment table.
 *
 * The virtual address space is divided into regions:
 * - Private A: Low addresses, per-process private segments
 * - Global A: Shared system segments (e.g., shared libraries, kernel)
 * - Private B: 8 additional per-process segments (stack, etc.)
 * - Global B: More shared segments
 *
 * The function maps the segment number from the VA to an index in the
 * segment table, which is organized differently from the virtual layout.
 *
 * Return values:
 * - default_result: Valid private segment, segno_out contains the index
 * - 0: Valid global segment, segno_out contains the index
 * - 0x3a: Invalid segment (falls in gap between regions)
 */

#include "mst.h"

/*
 * MST_$VA_TO_SEGNO - Convert virtual address to segment number
 *
 * @param virtual_addr   The virtual address to translate
 * @param segno_out      Output: segment table index
 * @param default_result Value to return for valid private segments
 * @return default_result for private, 0 for global, 0x3a for invalid
 */
uint16_t MST_$VA_TO_SEGNO(uint32_t virtual_addr, uint16_t *segno_out,
                           uint16_t default_result)
{
    uint16_t va_segno;
    uint16_t table_index;

    /*
     * Extract segment number from virtual address.
     * Each segment covers 32KB (0x8000 bytes), so segment = VA >> 15.
     */
    va_segno = (uint16_t)(virtual_addr >> 15);

    /*
     * Check if segment is within addressable memory.
     * MST__SEG_MEM_TOP marks the end of the virtual address space.
     */
    if (va_segno >= MST__SEG_MEM_TOP) {
        /* Beyond addressable memory - return default but don't set segno_out */
        return 0;
    }

    /*
     * Private A region: segments 0 to MST__PRIVATE_A_SIZE-1
     * These map directly to table indices 0..MST__PRIVATE_A_SIZE-1
     */
    if (va_segno < MST__PRIVATE_A_SIZE) {
        *segno_out = va_segno;
        return default_result;
    }

    /*
     * Private B region: 8 segments starting at MST__SEG_PRIVATE_B
     * These map to table indices MST__PRIVATE_A_SIZE..MST__PRIVATE_A_SIZE+7
     */
    if ((uint16_t)(va_segno - MST__SEG_PRIVATE_B) < 8) {
        *segno_out = MST__PRIVATE_A_SIZE + (va_segno - MST__SEG_PRIVATE_B);
        return default_result;
    }

    /*
     * Global A region: MST__GLOBAL_A_SIZE segments starting at MST__SEG_GLOBAL_A
     * Table index is the offset from MST__SEG_GLOBAL_A
     */
    table_index = va_segno - MST__SEG_GLOBAL_A;
    if (table_index < MST__GLOBAL_A_SIZE) {
        *segno_out = table_index;
        return 0;  /* Global segment - return 0 */
    }

    /*
     * Check for invalid gap between Global A end and Global B start
     */
    if (va_segno < MST__SEG_GLOBAL_B) {
        return 0x3a;  /* Invalid segment in gap */
    }

    /*
     * Global B region: segments starting at MST__SEG_GLOBAL_B
     * Table index continues after Global A entries
     */
    *segno_out = MST__GLOBAL_A_SIZE + (va_segno - MST__SEG_GLOBAL_B);
    return 0;  /* Global segment - return 0 */
}
