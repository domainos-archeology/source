/*
 * smd/interrupt_init.c - SMD_$INTERRUPT_INIT implementation
 *
 * Initialize SMD interrupt handling.
 *
 * Original address: 0x00E27284
 *
 * Assembly:
 *   lea (-0x366,PC),A0          ; A0 = &SMD_$DISP1_INT (0x00E26F1E)
 *   lea (0xe24c78).l,A1         ; A1 = PEB base
 *   tst.b (0x1a,A1)             ; Check PEB flag at offset 0x1A
 *   beq.w use_vector            ; If zero, use interrupt vector
 *   move.l A0,(0x00e24478).l    ; Store in PEB_$DISP_INT_ADDR
 *   bra.w done
 * use_vector:
 *   move.l A0,(0x00000070).l    ; Store in interrupt vector 0x70
 * done:
 *   rts
 *
 * This sets up the display interrupt handler either via the PEB (Process
 * Environment Block) or directly in the interrupt vector table, depending
 * on system configuration.
 */

#include "smd/smd_internal.h"
#include "peb/peb.h"

/* Display interrupt handler address */
extern void SMD_$DISP1_INT(void);

/* PEB base address */
#define PEB_BASE            0x00E24C78

/* PEB flag that determines interrupt setup method */
#define PEB_USE_VECTOR_FLAG 0x1A

/* PEB display interrupt address field */
extern void **PEB_$DISP_INT_ADDR;  /* at 0x00E24478 */

/* Interrupt vector for display (vector 0x1C, address 0x70) */
#define DISP_INT_VECTOR_ADDR    ((void **)0x00000070)

/*
 * SMD_$INTERRUPT_INIT - Initialize display interrupt handling
 *
 * Sets up the display interrupt handler. Depending on system configuration,
 * the handler is installed either:
 * - Via the PEB (Process Environment Block) interrupt address field
 * - Directly in the interrupt vector table at vector 0x70
 *
 * This function is called during SMD initialization.
 */
void SMD_$INTERRUPT_INIT(void)
{
    uint8_t *peb_base;
    uint8_t peb_flag;

    peb_base = (uint8_t *)PEB_BASE;
    peb_flag = peb_base[PEB_USE_VECTOR_FLAG];

    if (peb_flag == 0) {
        /* Use direct interrupt vector */
        *DISP_INT_VECTOR_ADDR = &SMD_$DISP1_INT;
    } else {
        /* Use PEB interrupt address */
        PEB_$DISP_INT_ADDR = (void **)&SMD_$DISP1_INT;
    }
}
