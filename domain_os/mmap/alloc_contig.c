/*
 * MMAP_$ALLOC_CONTIG - Allocate contiguous pages (stub)
 *
 * This function is a stub that always fails. Contiguous page
 * allocation is not supported in this version.
 *
 * Original address: 0x00e0d8f0
 */

#include "mmap.h"

void MMAP_$ALLOC_CONTIG(uint16_t count, uint32_t *pages_alloced, status_$t *status)
{
    (void)count;  /* Unused */

    *pages_alloced = 0;
    *status = status_$mmap_contig_pages_unavailable;
}
