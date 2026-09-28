/*
 * flp/do_io.c - FLP_$DO_IO (0x00E3DFE2, 32 bytes)
 *
 * The jump-table entry (FLP_$JUMP_TABLE[4], +0x10) DISK_$DO_IO calls with
 * its own four arguments.  It reserves a word result slot (`subq.l #0x2,SP`
 * at 0x00E3DFE6), pushes result, a zero word, param_3, req and vol, and
 * calls FLP_DO_IO; nothing reads the slot or D0 afterwards, so in C both
 * routines are procedures.
 */

#include "flp/flp_internal.h"

void FLP_$DO_IO(disk_$volume_t *vol, struct disk_io_req_t *req, void *param_3,
                int8_t *result)
{
    /* 0x00E3DFE8-0x00E3DFFA */
    FLP_DO_IO(vol, req, param_3, 0, result);
}
