/*
 * CACHE_$FLUSH_VIRTUAL - Flush virtual address cache
 *
 * On SAU2 (68010-based systems), this is a no-op since the 68010 has no
 * data cache. The function exists for API compatibility with later
 * processors (68020/030/040) that do have data caches requiring explicit
 * flushing for DMA coherency.
 *
 * On systems with data caches, this would typically:
 * - Push modified (dirty) cache lines to memory
 * - Invalidate cache entries for the specified virtual address range
 * - Ensure DMA operations see current memory contents
 *
 * Since 68010 has no cache, we simply return.
 *
 * Original address: 0x00E242E2
 * Size: 2 bytes
 *
 * Assembly:
 *   00e242e2    rts                 ; Return immediately (no-op)
 */

    .text
    .globl  CACHE_$FLUSH_VIRTUAL
    .type   CACHE_$FLUSH_VIRTUAL, @function

CACHE_$FLUSH_VIRTUAL:
    /*
     * No operation needed on 68010 - just return.
     * On 68020/030/040 this would use CPUSH or similar instructions.
     */
    rts

    .size   CACHE_$FLUSH_VIRTUAL, .-CACHE_$FLUSH_VIRTUAL
