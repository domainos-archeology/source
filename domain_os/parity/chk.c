/*
 * PARITY_$CHK - Handle memory parity error
 *
 * This function is called from FIM_$PARITY_TRAP to diagnose and
 * handle a memory parity error.
 *
 * Original address: 0x00E0AE68
 * Size: 770 bytes
 */

#include "parity/parity_internal.h"

/*
 * PARITY_$CHK
 *
 * Returns:
 *   0xFF (-1): Error recovered successfully (caller should return normally)
 *   0x00: Error not recovered (caller needs special handling)
 */
int8_t PARITY_$CHK(void)
{
    int8_t result;
    int8_t did_install;          /* True if we installed a temporary mapping */
    int8_t is_sau1;              /* True if SAU1 (68020-based), false for SAU2 */
    uint32_t err_status_long;    /* Full status from hardware */
    uint16_t saved_prot;         /* Original protection bits */
    uint16_t saved_asid;         /* Original ASID */
    uint16_t word_index;         /* Index of bad word in page */
    uint16_t byte_offset;        /* Byte offset adjustment */
    uint16_t err_data;           /* Data read from error location */
    uint32_t *pmape_ptr;         /* Pointer to PMAPE entry */
    uint16_t *scan_ptr;          /* Pointer for page scanning */
    int16_t scan_count;          /* Counter for scanning loop */

    result = 0xFF;               /* Assume success */
    did_install = 0;
    byte_offset = 0;

    /* Initialize error tracking state */
    PARITY_$ERR_PA = 0;
    PARITY_$ERR_VA = 0;
    PARITY_$ERR_DATA = 0;

    /*
     * Check for re-entrant call or ongoing parity error.
     * If we're already processing a parity error, or if the MMU
     * status indicates a real parity error (bit 1), crash the system.
     */
    if (PARITY_$CHK_IN_PROGRESS < 0 || (MMU_STATUS_REG & 0x02) != 0) {
        CRASH_SYSTEM(&Fault_Memory_Parity_Err);
    }

    /* Mark that we're now processing a parity error */
    PARITY_$CHK_IN_PROGRESS = 0xFF;

    /*
     * Determine MMU type from status register bit 0.
     * SAU1 (68020-based): bit 0 = 1
     * SAU2 (68010-based): bit 0 = 0
     */
    is_sau1 = (MMU_STATUS_REG & 0x01) ? -1 : 0;

    if (is_sau1) {
        /*
         * SAU1 (68020-based) error register handling.
         * Error info is at 0xFFB406 (word).
         */
        PARITY_$ERR_STATUS = MEM_ERR_STATUS_WORD;

        /* Check for valid error - bits 1 and 2 indicate byte lane errors */
        if (!((MEM_ERR_STATUS_WORD & SAU1_ERR_BYTE_UPPER) ||
              (MEM_ERR_STATUS_WORD & SAU1_ERR_BYTE_LOWER))) {
            /* No valid error bits set - spurious */
            goto spurious_error;
        }

        /* Extract page frame number (bits 4-15) and shift to get PPN */
        PARITY_$ERR_PPN = (uint32_t)(MEM_ERR_STATUS_WORD & SAU1_PPN_MASK) >> SAU1_PPN_SHIFT;
        /* Physical address is PPN << 10 (1KB pages) */
        PARITY_$ERR_PA = PARITY_$ERR_PPN << 10;

        /* Clear error status */
        MEM_ERR_STATUS_WORD = 0;

        /* Check if error occurred during DMA */
        PARITY_$DURING_DMA = (MEM_ERR_STATUS_WORD & SAU1_ERR_DMA) ? -1 : 0;
    } else {
        /*
         * SAU2 (68010-based) error register handling.
         * Error info is a 32-bit value at 0xFFB404.
         */
        err_status_long = MEM_ERR_STATUS_LONG;
        PARITY_$ERR_STATUS = (uint16_t)(err_status_long >> 16);

        /* Check low nibble - 0xF means no error */
        if (((err_status_long >> 8) & SAU2_ERR_LANE_MASK) == SAU2_ERR_NO_ERROR) {
            /* No error indicated - spurious */
            goto spurious_error;
        }

        /*
         * Extract physical address.
         * Bits 12-27 contain the page frame << 2, so shift right 12 and left 2
         * to get the page frame, then shift left 10 for physical address.
         */
        PARITY_$ERR_PA = (err_status_long >> SAU2_PPN_SHIFT) << 2;
        PARITY_$ERR_PPN = PARITY_$ERR_PA >> 10;

        /* Clear error status by writing to the word register */
        MEM_ERR_STATUS_WORD = 0;

        /* Check if error occurred during DMA (bits 4 or 5) */
        PARITY_$DURING_DMA = ((err_status_long & SAU2_ERR_DMA_UPPER) ||
                              (err_status_long & SAU2_ERR_DMA_LOWER)) ? -1 : 0;
    }

    /*
     * If error occurred during DMA, we cannot reliably diagnose it.
     * Log it and return failure.
     */
    if (PARITY_$DURING_DMA < 0) {
        result = 0;
        goto log_error;
    }

    /*
     * Convert physical page to virtual address.
     */
    PARITY_$ERR_VA = MMU_$PTOV(PARITY_$ERR_PPN);

    /*
     * Get the PMAPE entry for this physical page to save protection info.
     * PMAPE is at base + (ppn * 4), with protection in bits 1-8 and ASID in bits 4-8.
     */
    pmape_ptr = (uint32_t*)((char*)MMU_PMAPE_BASE + (PARITY_$ERR_PPN << 2));
    saved_prot = (*(uint8_t*)pmape_ptr >> 1) & 0x7F;
    saved_asid = (*pmape_ptr & PMAPE_PROT_MASK) >> PMAPE_PROT_SHIFT;

    /*
     * Install the error page at scratch location to read it safely.
     * Protection 0x16 = supervisor read/write.
     */
    MMU_$INSTALL(PARITY_$ERR_PPN, (uint32_t)PARITY_SCRATCH_PAGE, PARITY_SCRATCH_PROT);
    did_install = -1;
    byte_offset = 0;

    if (is_sau1) {
        /*
         * SAU1: Scan the entire page looking for which word triggers the error.
         * We read each word and check if the error registers indicate a problem.
         */
        scan_count = 0x1FF;  /* 512 words in a 1KB page */
        word_index = 0;
        scan_ptr = PARITY_SCRATCH_PAGE;

        do {
            err_data = *scan_ptr;

            /* Check if reading this word triggered a parity error */
            if ((MEM_ERR_STATUS_LONG & 0x04000000) ||  /* Upper byte error */
                (MEM_ERR_STATUS_LONG & 0x02000000)) {  /* Lower byte error */

                /* Determine which byte within the word */
                byte_offset = (MEM_ERR_STATUS_LONG & 0x02000000) ? 1 : 0;

                /* Calculate full offset into page */
                word_index = word_index * 2 + byte_offset;

                /* Update addresses with exact location */
                PARITY_$ERR_VA += word_index;
                PARITY_$ERR_PA += word_index;
                PARITY_$ERR_DATA = err_data;

                /* Clear error status */
                MEM_ERR_STATUS_WORD = 0;

                goto handle_error;
            }

            word_index++;
            scan_ptr++;
            scan_count--;
        } while (scan_count >= 0);

        /* Scanned entire page, no error found - spurious */
        goto spurious_error;

    } else {
        /*
         * SAU2: We may already know which word from the error address.
         * Use the low bits of the physical address.
         */
        uint8_t status_byte = (uint8_t)(err_status_long >> 8);

        /* Check if both byte lanes had error (bits 0-1 == 3) */
        word_index = (status_byte & ERR_BYTE_MASK) == ERR_BYTE_BOTH ? 1 : 0;

        /* Get word offset from physical address low bits */
        word_index += (PARITY_$ERR_PA & 0x3FF) >> 1;

        /* Read the word at that location */
        scan_ptr = PARITY_SCRATCH_PAGE;
        err_data = scan_ptr[word_index];

        /* Check low nibble of status - if 0xF, no error found */
        if ((MEM_ERR_STATUS_WORD & 0x0F) == 0x0F) {
            goto spurious_error;
        }

        /* Determine byte offset - check if even byte is OK (means odd byte bad) */
        byte_offset = ((MEM_ERR_STATUS_WORD & ERR_BYTE_EVEN_OK) != ERR_BYTE_EVEN_OK) ? 1 : 0;

        /* Update addresses and data */
        PARITY_$ERR_PA += byte_offset;
        PARITY_$ERR_VA += word_index * 2 + byte_offset;
        PARITY_$ERR_DATA = err_data;

        /* Clear error status */
        MEM_ERR_STATUS_WORD = 0;

        goto handle_error;
    }

spurious_error:
    /*
     * Spurious parity error - no real error found.
     * Increment counter and check for overflow.
     */
    PARITY_$INFO++;
    result = 0;

    if (PARITY_$INFO == 0) {
        /* Counter overflowed - too many spurious errors */
        CRASH_SYSTEM(&Fault_Spurious_Parity_Err);
    }

    goto finish;

handle_error:
    /*
     * Try to remove the corrupted page from the system.
     * AST_$REMOVE_CORRUPTED_PAGE returns:
     *   < 0: Page could not be recovered (data was modified)
     *   >= 0: Page was recovered or removed successfully
     */
    if (AST_$REMOVE_CORRUPTED_PAGE(PARITY_$ERR_PPN) < 0) {
        /* Cannot recover - page had modifications */
        did_install = 0;
    } else {
        /*
         * Page was handled. If it was read-only (saved_prot == 0),
         * crash since we can't reinstall it properly.
         */
        if (saved_prot == 0) {
            CRASH_SYSTEM(&Fault_Memory_Parity_Err);
        }

        /* Re-write the data to potentially re-trigger error for logging */
        scan_ptr = PARITY_SCRATCH_PAGE;
        scan_ptr[word_index] = err_data;
    }

log_error:
    MEM_$PARITY_LOG(PARITY_$ERR_PA);

finish:
    /* Log the error to system log */
    {
        parity_log_entry_t log_entry;
        log_entry.status = PARITY_$ERR_STATUS;
        log_entry.phys_addr = PARITY_$ERR_PA;
        log_entry.virt_addr = PARITY_$ERR_VA;
        LOG_$ADD(3, &log_entry, sizeof(log_entry));
    }

    /* Re-enable parity checking based on MMU type */
    if (is_sau1) {
        *(volatile uint8_t*)0xFFB404 = 0x01;
    } else {
        MEM_ERR_STATUS_WORD |= 0x40;
    }

    /* Restore original MMU mapping if we installed one */
    if (did_install < 0) {
        if (PARITY_$ERR_VA == 0) {
            /* No virtual address - just remove the mapping */
            MMU_$REMOVE(PARITY_$ERR_PPN);
        } else {
            /* Restore original mapping with saved protection */
            MMU_$INSTALL(PARITY_$ERR_PPN, PARITY_$ERR_VA,
                         ((uint32_t)saved_prot << 8) | saved_asid);
        }
    }

    /* Clear instruction cache since page contents may have changed */
    CACHE_$CLEAR();

    return result;
}
