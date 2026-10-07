/*
 * disk/chksum_page.c - disk_$chksum_page
 *
 * Original address: 0x00E0A290, 130 bytes.  Was FUN_00e0a290; renamed in
 * Ghidra as part of bead source-cm2w.
 *
 * Checksums one physical page.  The page is not necessarily mapped anywhere
 * the kernel can read it, so the routine maps it at a fixed scratch virtual
 * address, sums it, and then puts the mapping back the way it found it.
 *
 * Called from three places in DISK_IO: stamping a write (0x00E3D6B6),
 * verifying a completed write (0x00E3D7EC) and the read-after-write scratch
 * page (0x00E3D866).
 *
 * Every basic block of the original is accounted for; the addresses in the
 * comments say which instructions each statement stands for.
 */

#include "disk/disk_internal.h"

#include "chksum/chksum.h"
#include "mmu/mmu.h"

/*
 * 0x00E0A2B2 / 0x00E0A2C6: the scratch virtual address the page is mapped at
 * while it is summed.
 */
#define DISK_CHKSUM_SCRATCH_VA 0x00FF8400u

/*
 * 0x00E0A2AE / 0x00E0A2DA: the MMU protection word both installs use, pushed
 * as a longword with `pea (0x16).w`.
 */
#define DISK_CHKSUM_MMU_FLAGS 0x16u

uint16_t disk_$chksum_page(uint32_t *ppn)
{
    uint32_t page;   /* (-0x8,A6) */
    uint32_t old_va; /* D3 */
    uint16_t sum;    /* D2 */

    page = *ppn; /* 0x00E0A298-0x00E0A29C */

    /*
     * 0x00E0A2A0-0x00E0A2AC: where the page is mapped now, or 0 if it is not
     * mapped at all.
     */
    old_va = MMU_$PTOV(page);

    /* 0x00E0A2AE-0x00E0A2C2 */
    MMU_$INSTALL(page, DISK_CHKSUM_SCRATCH_VA, 0, DISK_CHKSUM_MMU_FLAGS);

    /* 0x00E0A2C6-0x00E0A2D4 */
    sum = CHKSUM_$GET_CHKSUM((const void *)DISK_CHKSUM_SCRATCH_VA);

    /* 0x00E0A2D6-0x00E0A2FA */
    if (old_va != 0) {
        MMU_$INSTALL(page, old_va, 0, DISK_CHKSUM_MMU_FLAGS); /* 0x00E0A2E4 */
    } else {
        MMU_$REMOVE(page); /* 0x00E0A2F4 */
    }

    /*
     * 0x00E0A2FC-0x00E0A306: the scratch mapping counts as a reference, so
     * the used bit is cleared before the page goes back to the pager.
     */
    MMU_$CLR_USED(page);

    return sum; /* move.w D2w,D0w */
}
