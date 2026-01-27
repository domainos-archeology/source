/*
 * peb/int.c - PEB Interrupt Handler
 *
 * Handles interrupts from the Performance Enhancement Board (PEB)
 * floating point accelerator.
 *
 * Original address: 0x00E2446C (110 bytes)
 *
 * Note: This is a low-level interrupt handler that uses assembly
 * language constructs that don't map directly to C. The actual
 * implementation would need to be in assembly for proper interrupt
 * handling (register saves, stack manipulation, RTE).
 */

#include "peb/peb_internal.h"
#include "dxm/dxm.h"
#include "io/io.h"

/*
 * DXM signal parameters for PEB interrupt
 * These are static data that define how to signal waiting processes
 * Original addresses: 0x00E244E4, 0x00E244EA, 0x00E244EC, 0x00E244E0
 */
static const uint16_t peb_dxm_signal_type = 0;    /* Signal type word */
static const uint16_t peb_dxm_signal_param = 0;   /* Signal parameter */
static const uint8_t peb_dxm_signal_flags = 0;    /* Signal flags */

/*
 * PEB_$INT - PEB Interrupt Handler
 *
 * Assembly analysis:
 *   00e2446c    btst.b #0x2,(0x00ff7001).l   ; Check interrupt pending bit
 *   00e24474    bne.b 0x00e2447c             ; If set, continue
 *                                            ; else fall through to spurious
 *   00e2447c    tst.b (0x00ff73fc).l         ; Touch PEB register
 *   00e24482    movem.l { A1 A0 D1 D0},-(SP) ; Save registers
 *   00e24486    move SR,D0w                  ; Save status register
 *   00e24488    ori #0x600,SR                ; Raise interrupt level
 *   00e2448c    jsr 0x00e2e826.l             ; IO_$USE_INT_STACK
 *   00e24492    move D0w,SR                  ; Restore SR
 *   00e24494    move.l (0x000070f4).w,D0     ; Read PEB exception status
 *   00e24498    move.l D0,(0x00e24468).l     ; Save to PEB_$STATUS_REG
 *   00e2449e    and.l #0x3f,D0               ; Mask exception bits
 *   00e244a4    bne.b 0x00e244b2             ; If any set, continue
 *   00e244a6    pea (0x3e,PC)                ; Push "PEB_interrupt"
 *   00e244aa    lea (0xe1e700).l,A0          ; CRASH_SYSTEM address
 *   00e244b0    jsr (A0)                     ; CRASH_SYSTEM("PEB_interrupt")
 *   00e244b2    pea (0x2c,PC)                ; Push status pointer
 *   ... (DXM_$ADD_SIGNAL call setup)
 *   00e244d0    jsr 0x00e17270.l             ; DXM_$ADD_SIGNAL
 *   00e244d6    adda.w #0x10,SP              ; Clean up stack
 *   00e244da    jmp 0x00e208fe.l             ; Jump to common interrupt exit
 *
 * Note: This function has complex control flow that continues into
 * the deferred interrupt handling code. The full logic includes
 * checking DI_$Q_HEAD and dispatching queued handlers.
 */
void PEB_$INT(void)
{
    uint32_t exc_status;
    status_$t status;

    /* Check if this is a real PEB interrupt */
    if ((PEB_STATUS_BYTE & PEB_STATUS_INTERRUPT) == 0) {
        /* Spurious interrupt - not from PEB */
        FIM_$SPURIOUS_INT();
        return;
    }

    /* Save registers and switch to interrupt stack */
    /* Note: In actual implementation, this would be done in assembly */
    IO_$USE_INT_STACK(0);  /* Parameter is SR value */

    /* Read and save PEB exception status from hardware register */
    /* Status register is at base + 0xF4 = 0x70F4 */
    exc_status = *(volatile uint32_t *)0x000070F4;
    PEB_$STATUS_REG = exc_status;

    /* Check if any exception bits are set */
    if ((exc_status & PEB_EXC_MASK) == 0) {
        /* No exception bits set - this shouldn't happen */
        CRASH_SYSTEM(&PEB_interrupt);
    }

    /* Signal waiting processes via DXM */
    /* The signal includes the AS ID of the affected process and the status */
    status = status_$peb_interrupt;
    DXM_$ADD_SIGNAL(
        peb_dxm_signal_param,           /* param1 */
        (uint16_t)PEB_$OWNER_ASID,      /* AS ID */
        peb_dxm_signal_type,            /* signal type */
        (uint32_t)status,               /* status value */
        peb_dxm_signal_flags,           /* flags */
        &status                         /* status pointer */
    );

    /* Exit through common interrupt exit code */
    /* This handles deferred interrupt processing and dispatch */
    FIM_$EXIT();
}
