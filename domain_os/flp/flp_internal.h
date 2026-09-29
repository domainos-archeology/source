/*
 * flp/flp_internal.h - Internal Floppy Driver Definitions
 *
 * Contains the module-local routines (EXCS, SHAKE, FLP_DO_IO,
 * FLP_FORMAT_TRACK), the `pea (d,PC)` constant cells of the FLP_ code
 * segment, and the hardware hooks.  External consumers use flp/flp.h.
 */

#ifndef FLP_INTERNAL_H
#define FLP_INTERNAL_H

#include "flp/flp.h"
#include "disk/disk_internal.h" /* disk_io_req_t - TODO: move it to disk.h once
                                 * disk/ is free (bead source-flp-req) */
#include "ml/ml.h"              /* ML_$LOCK / ML_$UNLOCK */
#include "wp/wp.h"              /* WP_$WIRE */
#include "mmu/mmu.h"            /* MMU_$VTOP */
#include "dma/dma.h"            /* DMA_$CHECK, the M68450 register offsets */
#include "parity/parity.h"      /* PARITY_$CHK_IO */
#include "time/time.h"          /* TIME_$CLOCKH */

/*
 * ============================================================================
 * Hardware access
 * ============================================================================
 */

/* The controller registers, through the base FLP_$CINIT / FLP_$DINIT /
 * FLP_DO_IO / FLP_$INT copy into FLP_DATA.hw_addr (`movea.l (0x12c,A5),An`). */
#define FLP_REGS()  ((volatile flp_regs_t *)ARCH_VA_TO_PTR(FLP_DATA.hw_addr))

/*
 * The FDC data register is the one register with side effects on access
 * (a read consumes a result byte, a write is a command byte), so the two
 * accesses are named; a host test scripting the FDC redefines them before
 * including the source.
 */
#ifndef FLP_FDC_READ_DATA
#define FLP_FDC_READ_DATA(regs)      ((regs)->data)
#define FLP_FDC_WRITE_DATA(regs, v)  ((regs)->data = (v))
#endif

/*
 * The DN300 DMAC: FLP_DO_IO (0x00E3DE5E `move.l #0xffa000,D4`) and
 * FLP_FORMAT_TRACK (0x00E3DD54) address it from 0xFFA000 with the channel-3
 * offsets 0xC5 (OCR), 0xC7 (CCR), 0xCA (MTC), 0xCC (MAR) and 0xE9 (MFC).
 * A SAU2 hardware address (arch/m68k/sau2/hw.h); a host test defines
 * SAU2_DMAC_BASE as the address of a plain 0x100-byte array.
 */
#define FLP_DMAC_BASE   ((volatile uint8_t *)SAU2_DMAC_BASE)

#define FLP_DMAC_CHAN3  (3 * DN300_DMAC_CHANNEL_SIZE)               /* 0xC0 */
#define FLP_DMAC_OCR    (*(FLP_DMAC_BASE + FLP_DMAC_CHAN3 + M68450_REG_OCR))   /* 0xC5 */
#define FLP_DMAC_CCR    (*(FLP_DMAC_BASE + FLP_DMAC_CHAN3 + M68450_REG_CCR))   /* 0xC7 */
#define FLP_DMAC_MTC    (*(volatile uint16_t *)(FLP_DMAC_BASE + FLP_DMAC_CHAN3 + M68450_REG_MTCH)) /* 0xCA */
#define FLP_DMAC_MAR    (*(volatile uint32_t *)(FLP_DMAC_BASE + FLP_DMAC_CHAN3 + M68450_REG_MARH)) /* 0xCC */
#define FLP_DMAC_MFC    (*(FLP_DMAC_BASE + FLP_DMAC_CHAN3 + M68450_REG_MFC))   /* 0xE9 */

/* The DMAC channel DMA_$CHECK is asked about (0x00E3E30A `move.w #0x3`). */
#define FLP_DMA_CHANNEL 3

/*
 * The per-process "I/O pending" byte the driver clears when it fails a
 * request: DISK_$DATA + 0x390 + pid*0x1C, i.e. disk_$per_proc_t.io_pending
 * indexed by the process id at req+0x1E (FLP_FORMAT_TRACK 0x00E3DD98-
 * 0x00E3DDB0, FLP_DO_IO 0x00E3DFA6-0x00E3DFBC; WIN uses the same code).
 */
#define FLP_IO_PENDING(pid) (DISK_$PER_PROC[(pid)].io_pending)

/*
 * ============================================================================
 * Literal cells in the FLP_ code region (map segment "I E3DC54 FLP_ size =
 * 8CC").  Domain Pascal passes VAR and const parameters by address, so each
 * literal argument becomes a cell in the code region whose address is pushed.
 * Defined in flp_data.c with the image bytes.
 * ============================================================================
 */

/* 0x00E3DDC2: 00 03 - the SEEK / SPECIFY / result word count */
extern int16_t flp_word_three;
/* 0x00E3DDC4: 00 06 - the FORMAT TRACK word count */
extern int16_t flp_word_six;
/* 0x00E3DFE0: 00 09 - the READ/WRITE DATA word count */
extern int16_t flp_word_nine;
/* 0x00E3E10E: 00 00 - SHAKE's "read" direction; io_$probe's first argument */
extern int16_t flp_word_zero;
/* 0x00E3E110: 00 01 - SHAKE's "write" direction, the count 1, and
 * DISK_$REGISTER's device type */
extern int16_t flp_word_one;
/* 0x00E3E21C: 00 02 - the RECALIBRATE / SENSE DRIVE STATUS word count */
extern int16_t flp_word_two;
/* 0x00E3E21E: 00 92 04 B2 00 00 00 01 00 00 - the label record FLP_$DINIT
 * copies out */
extern flp_pvlabel_info_t flp_dinit_pvlabel;

/*
 * ============================================================================
 * Internal routines
 * ============================================================================
 */

/*
 * SHAKE (0x00E3E49E) - move *count_ptr bytes between `data` (one byte per
 * word) and the FDC data register, in the direction *dir_ptr (0 = read,
 * 1 = write), insisting the FDC's DIO bit agrees.  status_$ok,
 * status_$disk_controller_timeout or status_$disk_controller_error.
 */
status_$t SHAKE(uint16_t *data, int16_t *count_ptr, int16_t *dir_ptr);

/*
 * EXCS (0x00E3E268) - execute the command at `cmd` (*count_ptr words),
 * wait for the interrupt, and interpret the result registers.  `vol` is the
 * volume whose as_options bit 1 decides whether a data check is retried; it
 * is passed on into the recursive RECALIBRATE.
 */
status_$t EXCS(uint16_t *cmd, int16_t *count_ptr, disk_$volume_t *vol);

/*
 * FLP_DO_IO (0x00E3DDC6) - the body FLP_$DO_IO gates to.  `zero` is the
 * word FLP_$DO_IO inserts; it is never read.
 */
void FLP_DO_IO(disk_$volume_t *vol, disk_io_req_t *req, void *param_3,
               int16_t zero, int8_t *result);

/*
 * FLP_FORMAT_TRACK (0x00E3DC78) - format the track a request names.  Does
 * not reload A5: it runs with FLP_DO_IO's module base.
 */
void FLP_FORMAT_TRACK(disk_$volume_t *vol, disk_io_req_t *req);

#endif /* FLP_INTERNAL_H */
