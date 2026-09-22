/*
 * DISK_$WAIT_QUE - Public gate onto disk_$wait_io
 *
 * 0x00E3CABA - 0x00E3CADE (38 bytes).  Re-emitted from the disassembly on
 * 2026-09-19: the earlier file was an empty stub with a made-up
 * (queue, status) signature.  The routine saves A5, loads A5 = DISK_$DATA
 * (0xE7A1CC) for the module-internal disk_$wait_io, opens a discarded
 * result slot and forwards its three arguments unchanged
 * (0x00E3CAC6 - 0x00E3CAD4), then restores A5.
 *
 * Arguments:
 *   (0x8,A6) disk_mask       word by value
 *   (0xa,A6) io_wait_val     -> int32_t
 *   (0xe,A6) error_wait_val  -> int32_t
 */

#include "disk/disk_internal.h"

void DISK_$WAIT_QUE(uint16_t disk_mask, int32_t *io_wait_val, int32_t *error_wait_val)
{
    disk_$wait_io(disk_mask, io_wait_val, error_wait_val);
}
