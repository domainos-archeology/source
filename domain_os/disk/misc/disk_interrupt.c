/*
 * DISK_INTERRUPT - Disk interrupt handler
 *
 * Called when a disk interrupt occurs. Determines which disk controller
 * caused the interrupt by checking status bits and dispatches to the
 * appropriate device handler.
 *
 * From: 0x00e0aabc
 *
 * The interrupt controller data is at 0xe22904:
 *   +0x10: Primary handler function pointer
 *   +0x14: Primary device controller table entry
 *   +0x20: Secondary handler function pointer
 *   +0x24: Secondary device controller table entry
 *
 * Device controller table entry layout:
 *   +0x34: Pointer to disk_dinit structure
 *          disk_dinit+0x06: Status register
 *
 * Status register bits:
 *   0x0400: Primary disk interrupt pending
 *   0x1800: Secondary disk interrupt pending
 *
 * Original assembly:
 *   00e0aabc    link.w A6,-0x8
 *   00e0aac0    pea (A5)
 *   00e0aac2    lea (0xe22904).l,A5
 *   00e0aac8    movea.l (0x14,A5),A1         ; Get primary DCTE
 *   00e0aacc    movea.l (0x34,A1),A0         ; Get disk_dinit ptr
 *   00e0aad0    move.w (0x6,A0),D0w          ; Read status register
 *   00e0aad4    btst.l #0xa,D0               ; Test bit 10 (0x400)
 *   00e0aad8    beq.b check_secondary
 *   00e0aada    move.l (0x14,A5),-(SP)       ; Push primary DCTE
 *   00e0aade    movea.l (0x10,A5),A1         ; Get primary handler
 *   00e0aae2    bra.b call_handler
 *   check_secondary:
 *   00e0aae4    andi.w #0x1800,D0w           ; Test bits 11-12
 *   00e0aae8    beq.b unrecognized           ; Neither set - crash
 *   00e0aaea    move.l (0x24,A5),-(SP)       ; Push secondary DCTE
 *   00e0aaee    movea.l (0x20,A5),A1         ; Get secondary handler
 *   call_handler:
 *   00e0aaf2    jsr (A1)                     ; Call handler
 *   00e0aaf4    bra.b done
 *   unrecognized:
 *   00e0aaf6    pea error_status
 *   00e0aafa    jsr CRASH_SYSTEM
 *   done:
 *   00e0ab00    ...
 */

#include "disk/disk_internal.h"
#include "misc/crash_system.h"

/*
 * Disk interrupt controller data structure
 * Located at 0xe22904
 */
typedef struct disk_int_ctrl_t {
    uint8_t reserved1[0x10];      /* +0x00: Reserved */
    void (*primary_handler)(void *dcte);   /* +0x10: Primary handler */
    void *primary_dcte;           /* +0x14: Primary device ctrl entry */
    uint8_t reserved2[0x08];      /* +0x18: Reserved */
    void (*secondary_handler)(void *dcte); /* +0x20: Secondary handler */
    void *secondary_dcte;         /* +0x24: Secondary device ctrl entry */
} disk_int_ctrl_t;

/*
 * Device controller table entry (partial)
 */
typedef struct dcte_t {
    uint8_t reserved[0x34];       /* +0x00: Reserved fields */
    void *disk_dinit;             /* +0x34: Pointer to dinit structure */
} dcte_t;

/*
 * Disk dinit structure (partial)
 */
typedef struct disk_dinit_t {
    uint8_t reserved[0x06];       /* +0x00: Reserved */
    uint16_t status;              /* +0x06: Status register */
} disk_dinit_t;

/* Interrupt controller data - at 0xe22904 */
#if defined(M68K)
#define DISK_INT_CTRL ((disk_int_ctrl_t *)0xe22904)
#else
extern disk_int_ctrl_t *disk_int_ctrl_ptr;
#define DISK_INT_CTRL disk_int_ctrl_ptr
#endif

/* Interrupt status bits */
#define DISK_INT_PRIMARY    0x0400  /* Primary disk interrupt */
#define DISK_INT_SECONDARY  0x1800  /* Secondary disk interrupt */

/* Error status for unrecognized interrupt */
static const status_$t Disk_unrecognized_interrupt_err = 0x00080032;

void DISK_INTERRUPT(void)
{
    dcte_t *dcte;
    disk_dinit_t *dinit;
    uint16_t status;
    void (*handler)(void *);
    void *handler_dcte;

    /* Get primary device controller entry */
    dcte = (dcte_t *)DISK_INT_CTRL->primary_dcte;
    dinit = (disk_dinit_t *)dcte->disk_dinit;

    /* Read status register */
    status = dinit->status;

    /* Check for primary disk interrupt (bit 10) */
    if ((status & DISK_INT_PRIMARY) != 0) {
        handler = DISK_INT_CTRL->primary_handler;
        handler_dcte = DISK_INT_CTRL->primary_dcte;
    }
    /* Check for secondary disk interrupt (bits 11-12) */
    else if ((status & DISK_INT_SECONDARY) != 0) {
        handler = DISK_INT_CTRL->secondary_handler;
        handler_dcte = DISK_INT_CTRL->secondary_dcte;
    }
    else {
        /* Unrecognized interrupt - fatal error */
        CRASH_SYSTEM(&Disk_unrecognized_interrupt_err);
        return;  /* Not reached - CRASH_SYSTEM doesn't return */
    }

    /* Dispatch to the appropriate handler */
    handler(handler_dcte);
}
