/*
 * flp/flp_data.c - FLP module data definitions
 *
 * Original M68K addresses:
 *   FLP_DATA        0x00E7AEF4  0x13C bytes ("D E7AEF4 FLP_ size = 13C")
 *   DAT_00e3ddc2    0x00E3DDC2  literal cells in the FLP_ code segment
 *   DAT_00e3ddc4    0x00E3DDC4
 *   DAT_00e3dfe0    0x00E3DFE0
 *   DAT_00e3e10e    0x00E3E10E
 *   DAT_00e3e110    0x00E3E110
 *   DAT_00e3e21c    0x00E3E21C
 *   DAT_00e3e21e    0x00E3E21E
 *   DAT_00e3e222    0x00E3E222
 *   DAT_00e3e226    0x00E3E226
 */

#include "flp/flp_internal.h"

/*
 * FLP_DATA - the floppy module's A5 block.  The layout is in flp/flp.h; the
 * values below are the image bytes from `gsk read 0x00E7AEF4 0x13C`.
 */
flp_data_t FLP_DATA = {
    /* +0x000 jump table: DISK_$REGISTER reads the entry points from here */
    .jump_table = {
        0x00000000,
        0x00E3E228,  /* FLP_$SHUTDOWN   */
        0x00E3E112,  /* FLP_$DINIT      */
        0x00E3DC54,  /* FLP_$REVALIDATE */
        0x00E3DFE2,  /* FLP_$DO_IO      */
        0x00000000,
        0x00000000,
    },
    /* +0x01C */
    .sregs_array = { 0x0000, 0x0000, 0x000d, 0x0000,
                     0x0000, 0x001a, 0x001b, 0x004e },
    /* +0x02C FORMAT TRACK: cmd 0x4D (MFM), unit/head, N=3, SC=8, GPL=0x74,
     * filler byte D=0x4E */
    .fmt_cmd = { 0x004d, 0x0000, 0x0003, 0x0008, 0x0074, 0x004e },
    /* +0x038 */
    .result_regs = { 0, 0, 0, 0, 0, 0x001a, 0x0007, 0x0080, 0x0000 },
    /* +0x04A READ/WRITE DATA: cmd, unit/head, cyl, head, sector, N=3, EOT=8,
     * GPL=0x35, DTL=0xFF */
    .rw_cmd = { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
                0x0003, 0x0008, 0x0035, 0x00ff },
    .w_05c = { 0x0000, 0x0000 },
    /* +0x060 FLP_$EC */
    .ec = { 0 },
    .w_06c = { 0x0000, 0x0000 },
    /* +0x070 FLP_$SREGS */
    .sregs = { 0x0000, 0x0000, 0x0000, 0x0000 },
    .unit_cyl = { 0, 0, 0, 0, 0, 0, 0, 0 },
    .io_buffer = { 0 },
    .ctlr_table = { 0 },
    .fmt_buf_pa = 0x00000000,
    .w_100 = 0x0000,
    .fmt_n = 0x0003,
    .w_104 = 0x0000,
    .base_cmd = 0x0040,
    /* +0x108 SPECIFY: cmd 0x03, SRT/HUT 0xDF, HLT/ND 0x3C */
    .specify_cmd = { 0x0003, 0x00df, 0x003c, 0x0000 },
    /* +0x110 SENSE DRIVE STATUS: cmd 0x04, unit/head */
    .sense_cmd = { 0x0004, 0x0000 },
    /* +0x114 RECALIBRATE: cmd 0x07, unit */
    .recal_cmd = { 0x0007, 0x0000 },
    /* +0x118 SEEK: cmd 0x0F, unit/head, cylinder */
    .seek_cmd = { 0x000f, 0x0000, 0x0000, 0x0000 },
    .unit_active = { 0, 0, 0, 0 },
    .disk_change = { 0, 0, 0, 0 },
    .buf_pa = 0x00000000,
    .hw_addr = 0x00000000,
    .dma_retry = 0,
    .cmd_retry = 0,
    .w_134 = 0x0000,
    .unit_count = 0x8000,
    .initialized = 0,
    .pad_139 = { 0, 0, 0 },
};

/*
 * ============================================================================
 * Literal cells in the FLP_ code region
 * ============================================================================
 *
 * Each sits immediately after an `rts` or just before the next routine, which
 * is what pins its extent:
 *   0x00E3DDC2/0x00E3DDC4  after the `rts` at 0x00E3DDC0, before FLP_DO_IO
 *                          (0x00E3DDC6).  Image bytes 00 03 00 06.
 *   0x00E3DFE0             after the `rts` at 0x00E3DFDE, before FLP_$DO_IO
 *                          (0x00E3DFE2).  Image bytes 00 09.
 *   0x00E3E10E/0x00E3E110  after the `rts` at 0x00E3E10C, before FLP_$DINIT
 *                          (0x00E3E112).  Image bytes 00 00 00 01.
 *   0x00E3E21C..0x00E3E228 after the `rts` at 0x00E3E21A, before
 *                          FLP_$SHUTDOWN.  Image bytes
 *                          00 02 | 00 92 04 b2 | 00 00 00 01 | 00 00.
 */

int16_t DAT_00e3ddc2 = 3;
int16_t DAT_00e3ddc4 = 6;
int16_t DAT_00e3dfe0 = 9;

int16_t DAT_00e3e10e = 0;
int16_t DAT_00e3e110 = 1;
int16_t DAT_00e3e21c = 2;

uint32_t DAT_00e3e21e = 0x009204B2;
uint32_t DAT_00e3e222 = 0x00000001;
uint16_t DAT_00e3e226 = 0x0000;
