/*
 * FLP - Floppy Disk Driver (NEC uPD765 / 8272 class FDC, DN3xx)
 *
 * Map: code segment `I E3DC54 FLP_ size = 8CC` holding FLP_$REVALIDATE
 * (0xE3DC54), FLP_FORMAT_TRACK (0xE3DC78), FLP_DO_IO (0xE3DDC6), FLP_$DO_IO
 * (0xE3DFE2), FLP_$CINIT (0xE3E002), FLP_$DINIT (0xE3E112), FLP_$SHUTDOWN
 * (0xE3E228), EXCS (0xE3E268) and SHAKE (0xE3E49E); FLP_$INT is a separate
 * segment `I E19F6C FLP_ size = AC`; the data block is `D E7AEF4 FLP_ size =
 * 13C` with the two interior symbols FLP_$EC (+0x60) and FLP_$SREGS (+0x70).
 *
 * The controller's registers are reached through the base address the DCTE
 * supplies (kept in FLP_DATA.hw_addr); the transfers use channel 3 of the
 * DN300 M68450 DMAC at 0xFFA000.
 */

#ifndef FLP_H
#define FLP_H

#include "base/base.h"
#include "ec/ec.h"
#include "disk/disk.h"      /* disk_$volume_t, disk_device_entry_t, status_$disk_* */
#include "io/io.h"          /* dcte_t, io_$probe, status_$io_controller_not_in_system */

/* The request record FLP_$DO_IO is handed (disk/disk_internal.h). */
struct disk_io_req_t;

/* Four drive units: FLP_$DINIT rejects unit > 3 (0x00E3E130). */
#define FLP_MAX_UNITS 4

/*
 * Floppy-specific status codes (stcode.db.10.2, module 8 = disk).  The
 * remaining codes the driver returns are the DISK ones in disk/disk.h.
 */
#define status_$floppy_is_not_2_sided               0x00080006
#define status_$bad_disk_format                     0x00080008
#define status_$unknown_status_returned_by_hardware 0x00080019
#define status_$dma_not_at_end_of_range             0x0008001d

/* Driver-internal "retry the command" marker EXCS hands back (0x00E3E48C)
 * and FLP_DO_IO loops on (0x00E3DF76).  Not a status-code database entry. */
#define FLP_$RETRY                                  0x0008ffff

/*
 * ============================================================================
 * Controller registers, byte offsets from FLP_DATA.hw_addr
 * ============================================================================
 */
typedef struct flp_regs_t {
  uint8_t  _pad_00[6];
  uint16_t w_06;        /* 0x06: EXCS tests bit 1 of this word (0x00E3E2DC)
                         *   before consulting the memory parity checker;
                         *   purpose otherwise unknown.  TODO: verify */
  uint8_t  _pad_08[8];
  uint8_t  status;      /* 0x10: FDC main status register */
  uint8_t  _pad_11;
  uint8_t  data;        /* 0x12: FDC data register */
  uint8_t  _pad_13;
  uint8_t  control;     /* 0x14: board control: 2 while reading, 3 otherwise */
} flp_regs_t;

_Static_assert(__builtin_offsetof(flp_regs_t, w_06) == 0x06, "flp_regs_t.w_06");
_Static_assert(__builtin_offsetof(flp_regs_t, status) == 0x10, "flp_regs_t.status");
_Static_assert(__builtin_offsetof(flp_regs_t, data) == 0x12, "flp_regs_t.data");
_Static_assert(__builtin_offsetof(flp_regs_t, control) == 0x14, "flp_regs_t.control");

/* Main status register bits */
#define FLP_STATUS_RQM      0x80    /* request for master: data register ready */
#define FLP_STATUS_DIO      0x40    /* data direction: set = FDC -> CPU */
#define FLP_STATUS_CMD_MASK 0x1F    /* command busy + four drive-busy bits */

/*
 * ============================================================================
 * The 10-byte record FLP_$DINIT fills in for the mounter
 * ============================================================================
 *
 * Copied from the constant at 0x00E3E21E (00 92 04 B2 | 00 00 00 01 | 00 00)
 * with `move.l (A0)+,(A1)+` twice and `move.w (A0)+,(A1)+` (0x00E3E202-
 * 0x00E3E206), after which w_06 is set to 1 unconditionally (0x00E3E208).
 */
typedef struct flp_pvlabel_info_t {
  uint32_t l_00;        /* 0x00: 0x009204B2 */
  uint16_t w_04;        /* 0x04: 0 */
  uint16_t w_06;        /* 0x06: 1 */
  uint16_t w_08;        /* 0x08: 0 */
} __attribute__((packed, aligned(2))) flp_pvlabel_info_t;

_Static_assert(sizeof(flp_pvlabel_info_t) == 10, "flp_pvlabel_info_t is 10 bytes");

/*
 * ============================================================================
 * FLP_DATA - the floppy module's data block at 0x00E7AEF4
 * ============================================================================
 *
 * Every FLP_ routine establishes it with `lea (0xe7aef4).l,A5` (FLP_$INT with
 * `movea.l #0xe7aef4,A2`), so all the cells the driver touches are fields of
 * this one block.  The image contents come from `gsk read 0x00E7AEF4 0x13C`;
 * the command blocks in it are NEC 8272 FDC command strings, one byte per
 * word (SHAKE writes the low byte of each word, 0x00E3E4FC):
 *
 *   +0x02C  4D 00 03 08 74 4E              FORMAT TRACK, 6 words
 *                                          (the count cell at 0x00E3DDC4)
 *   +0x04A  00 00 00 00 00 03 08 35 FF     READ/WRITE DATA, 9 words
 *                                          (the count cell at 0x00E3DFE0);
 *                                          its first 3 words double as SEEK
 *   +0x108  03 DF 3C                       SPECIFY, 3 words
 *   +0x110  04 00                          SENSE DRIVE STATUS, 2 words
 *   +0x114  07 00                          RECALIBRATE, 2 words
 *   +0x118  0F 00 00                       SEEK, 3 words (format path)
 */

/* One controller's slot in the table at +0x0E8: FLP_$CINIT stores the DCTE
 * and its register base at +0xE8 / +0xEC + ctlr*8 (0x00E3E048-0x00E3E050). */
typedef struct flp_ctlr_entry_t {
  uint32_t dcte_va;     /* +0: the dcte_t FLP_$CINIT was given (32-bit VA) */
  uint32_t hw_addr;     /* +4: dcte->disk_dinit, the register base */
} flp_ctlr_entry_t;

typedef struct flp_data_t {
  /* +0x000 FLP_$JUMP_TABLE: the driver entry points DISK_$REGISTER is given */
  m68k_ptr_t jump_table[7];
  /* +0x01C FLP_$SREGS_ARRAY (map name); not referenced by the code */
  uint16_t sregs_array[8];
  /* +0x02C FORMAT TRACK command block: cmd, unit/head, N, SC, GPL, D */
  uint16_t fmt_cmd[6];
  /* +0x038 nine words, not referenced by the code */
  uint16_t w_038[9];
  /* +0x04A READ/WRITE DATA command block: cmd, unit/head, cyl, head, sector,
   * N, EOT, GPL, DTL.  The first three words are also the SEEK command
   * FLP_DO_IO sends (0x00E3DEBE). */
  uint16_t rw_cmd[9];
  uint16_t w_05c[2];              /* +0x05C */
  ec_$eventcount_t ec;            /* +0x060 FLP_$EC */
  uint16_t w_06c[2];              /* +0x06C */
  uint16_t sregs[4];              /* +0x070 FLP_$SREGS: the result bytes
                                   * FLP_$INT collects, one per word */
  uint16_t unit_cyl[FLP_MAX_UNITS];  /* +0x078 current cylinder per unit */
  uint8_t io_buffer[0x68];        /* +0x080 the format-table buffer */
  flp_ctlr_entry_t ctlr_table[2]; /* +0x0E8 */
  uint32_t l_0f8;                 /* +0x0F8 */
  uint32_t fmt_buf_pa;            /* +0x0FC physical address of io_buffer */
  uint16_t w_100;                 /* +0x100 */
  uint16_t fmt_n;                 /* +0x102 sector-size code N; only its low
                                   * byte (+0x103) goes into the format table */
  uint16_t w_104;                 /* +0x104 */
  uint16_t base_cmd;              /* +0x106 MFM base command byte (0x40) */
  uint16_t specify_cmd[4];        /* +0x108 SPECIFY command block */
  uint16_t sense_cmd[2];          /* +0x110 SENSE DRIVE STATUS: cmd, unit/head */
  uint16_t recal_cmd[2];          /* +0x114 RECALIBRATE: cmd, unit */
  uint16_t seek_cmd[4];           /* +0x118 SEEK: cmd, unit/head, cylinder */
  int8_t unit_active[FLP_MAX_UNITS];   /* +0x120 Domain booleans */
  int8_t disk_change[FLP_MAX_UNITS];   /* +0x124 Domain booleans */
  uint32_t buf_pa;                /* +0x128 the request's page (ppn) */
  uint32_t hw_addr;               /* +0x12C controller register base (VA) */
  int16_t dma_retry;              /* +0x130 DMA-overrun retries left */
  int16_t cmd_retry;              /* +0x132 command retries left */
  uint16_t w_134;                 /* +0x134 */
  uint16_t unit_count;            /* +0x136 DISK_$REGISTER's unit-count word */
  int8_t initialized;             /* +0x138 -1 once FLP_$DINIT wired the buffer */
  uint8_t pad_139[3];             /* +0x139 */
} flp_data_t;

/* Everything up to the eventcount is pointer-free, so those offsets hold on
 * every host; ec_$eventcount_t carries two native pointers, so the rest of
 * the layout is only checked on the target. */
_Static_assert(__builtin_offsetof(flp_data_t, sregs_array) == 0x01C, "flp_data_t.sregs_array");
_Static_assert(__builtin_offsetof(flp_data_t, fmt_cmd) == 0x02C, "flp_data_t.fmt_cmd");
_Static_assert(__builtin_offsetof(flp_data_t, w_038) == 0x038, "flp_data_t.w_038");
_Static_assert(__builtin_offsetof(flp_data_t, rw_cmd) == 0x04A, "flp_data_t.rw_cmd");
_Static_assert(__builtin_offsetof(flp_data_t, ec) == 0x060, "flp_data_t.ec");
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(flp_data_t, sregs) == 0x070, "flp_data_t.sregs");
_Static_assert(__builtin_offsetof(flp_data_t, unit_cyl) == 0x078, "flp_data_t.unit_cyl");
_Static_assert(__builtin_offsetof(flp_data_t, io_buffer) == 0x080, "flp_data_t.io_buffer");
_Static_assert(__builtin_offsetof(flp_data_t, ctlr_table) == 0x0E8, "flp_data_t.ctlr_table");
_Static_assert(__builtin_offsetof(flp_data_t, l_0f8) == 0x0F8, "flp_data_t.l_0f8");
_Static_assert(__builtin_offsetof(flp_data_t, fmt_buf_pa) == 0x0FC, "flp_data_t.fmt_buf_pa");
_Static_assert(__builtin_offsetof(flp_data_t, fmt_n) == 0x102, "flp_data_t.fmt_n");
_Static_assert(__builtin_offsetof(flp_data_t, base_cmd) == 0x106, "flp_data_t.base_cmd");
_Static_assert(__builtin_offsetof(flp_data_t, specify_cmd) == 0x108, "flp_data_t.specify_cmd");
_Static_assert(__builtin_offsetof(flp_data_t, sense_cmd) == 0x110, "flp_data_t.sense_cmd");
_Static_assert(__builtin_offsetof(flp_data_t, recal_cmd) == 0x114, "flp_data_t.recal_cmd");
_Static_assert(__builtin_offsetof(flp_data_t, seek_cmd) == 0x118, "flp_data_t.seek_cmd");
_Static_assert(__builtin_offsetof(flp_data_t, unit_active) == 0x120, "flp_data_t.unit_active");
_Static_assert(__builtin_offsetof(flp_data_t, disk_change) == 0x124, "flp_data_t.disk_change");
_Static_assert(__builtin_offsetof(flp_data_t, buf_pa) == 0x128, "flp_data_t.buf_pa");
_Static_assert(__builtin_offsetof(flp_data_t, hw_addr) == 0x12C, "flp_data_t.hw_addr");
_Static_assert(__builtin_offsetof(flp_data_t, dma_retry) == 0x130, "flp_data_t.dma_retry");
_Static_assert(__builtin_offsetof(flp_data_t, cmd_retry) == 0x132, "flp_data_t.cmd_retry");
_Static_assert(__builtin_offsetof(flp_data_t, unit_count) == 0x136, "flp_data_t.unit_count");
_Static_assert(__builtin_offsetof(flp_data_t, initialized) == 0x138, "flp_data_t.initialized");
_Static_assert(sizeof(flp_data_t) == 0x13C,
               "flp_data_t: map segment FLP_ 0x00E7AEF4 size = 13C");
#endif

extern flp_data_t FLP_DATA;

/* Event counter for floppy operations (FLP_DATA + 0x60 = 0xE7AF54) */
#define FLP_$EC (FLP_DATA.ec)

/* FDC result status registers (FLP_DATA + 0x70 = 0xE7AF64) */
#define FLP_$SREGS (FLP_DATA.sregs)

/* Jump table for floppy operations (FLP_DATA + 0x00 = 0xE7AEF4) */
#define FLP_$JUMP_TABLE (FLP_DATA.jump_table)

/*
 * ============================================================================
 * Entry points (all reached through FLP_$JUMP_TABLE except CINIT and INT)
 * ============================================================================
 */

/*
 * FLP_$CINIT (0x00E3E002) - controller initialisation
 *
 * Probes the controller at dcte->disk_dinit, records it in the controller
 * table, drains the FDC, sends SPECIFY and registers with DISK.  Returns
 * status_$io_controller_not_in_system, status_$disk_controller_error (the
 * FDC never went idle), SHAKE's status, or status_$ok.
 */
status_$t FLP_$CINIT(dcte_t *dcte);

/*
 * FLP_$DINIT (0x00E3E112) - unit initialisation (jump table +0x08)
 *
 * Recalibrates `unit` on controller `ctlr`.  When that succeeds and
 * *num_blocks <= 0 the geometry is filled in: 0x4D0 blocks, 8 sectors per
 * track, 2 heads, flags 0, and the 10-byte label record from 0x00E3E21E.
 * pvlabel_info->w_06 is set to 1 on every path, even failures.
 */
status_$t FLP_$DINIT(uint16_t unit, uint16_t ctlr, int32_t *num_blocks,
                     uint16_t *sec_per_track, uint16_t *num_heads,
                     flp_pvlabel_info_t *pvlabel_info, uint16_t *flags);

/*
 * FLP_$SHUTDOWN (0x00E3E228) - jump table +0x04, (controller, unit)
 *
 * Clears unit_active[unit] and returns how many of the four units are still
 * active.  `ctlr` is (0x8,A6) and never read (0x00E3E236 takes (0xa,A6)).
 */
int16_t FLP_$SHUTDOWN(uint16_t ctlr, uint16_t unit);

/*
 * FLP_$INT (0x00E19F6C) - interrupt handler
 *
 * Reads the result phase (issuing SENSE INTERRUPT STATUS first when the FDC
 * is not already presenting results), keeps the first three bytes in
 * FLP_$SREGS, flags a disk change, and advances FLP_$EC.  Returns Domain
 * true (`st D0b`, 0x00E1A00C).
 */
int8_t FLP_$INT(dcte_t *dcte);

/*
 * FLP_$REVALIDATE (0x00E3DC54) - jump table +0x0C
 *
 * Clears the disk-change flag of the volume's unit.
 */
void FLP_$REVALIDATE(disk_$volume_t *vol);

/*
 * FLP_$DO_IO (0x00E3DFE2) - jump table +0x10
 *
 * Gate that calls FLP_DO_IO with the same four arguments plus a zero word
 * inserted before `result` (0x00E3DFEC `clr.w -(SP)`).
 */
void FLP_$DO_IO(disk_$volume_t *vol, struct disk_io_req_t *req, void *param_3,
                int8_t *result);

#endif /* FLP_H */
