/*
 * MMAP_$ALLOC_CONTIG - Allocate contiguous pages (stub)
 *
 * Original address: 0x00E0D8F0 (36 bytes; `E0D8F0 MMAP_$ALLOC_CONTIG` in the
 * SAU2 map).  No caller in the image (`gsk xrefs to 00e0d8f0` is empty).
 *
 * Frame (0x00E0D8F0-0x00E0D8FC): `link.w A6,#0`, A5 saved and pointed at the
 * MMAP_ block (0xE23284).  Arguments, (0x8,A6) being argument 1:
 *   (0x8,A6)  num_pages     word, never read
 *   (0xA,A6)  pages_alloced longword pointer
 *   (0xE,A6)  status        longword pointer
 *
 * Body (0x00E0D8FC-0x00E0D90A): `clr.l (A0)` on pages_alloced, then
 * `move.l #0x6000e,(A1)` - status_$mmap_contig_pages_unavailable, "contiguous
 * pages unavailable" in the SR10.2 status-code database.  Nothing is
 * allocated; the routine is an always-fails stub.
 */

#include "mmap/mmap_internal.h"

void MMAP_$ALLOC_CONTIG(uint16_t num_pages, uint32_t *pages_alloced,
                        status_$t *status)
{
    (void)num_pages; /* (0x8,A6) is never read */

    *pages_alloced = 0;                                /* 0x00E0D900 */
    *status = status_$mmap_contig_pages_unavailable;   /* 0x00E0D906 */
}
