/*
 * pmap_$write_page - Write a single page to disk or network
 *
 * Writes a dirty page to its backing store, which may be either
 * local disk or a remote network node.
 *
 * Process:
 * 1. Look up physical page info from VPN (page frame table at 0xEB4800)
 * 2. Determine if page is remote (network-backed) or local (disk-backed)
 * 3. For remote pages:
 *    - Set up network write parameters (partner info, packet size)
 *    - Handle sub-page writes for small packet sizes
 *    - Call NETWORK_$WRITE
 *    - On parity error: save clobbered UID
 *    - On success: update network sequence numbers
 * 4. For local pages:
 *    - Extract disk address (masked to 22 bits)
 *    - Call DISK_$WRITE
 *    - Ignore write-protected errors
 * 5. On success: call pmap_$write_complete to update page state, advance PMAP EC
 * 6. On failure: handle various error cases (invalidate, crash, etc.)
 *
 * Uses lock 14 (PMAP lock) - unlocks before I/O, re-locks after.
 *
 * Parameters:
 *   vpn       - Virtual page number to write
 *   status    - Output: status code
 *   sync_flag - Negative for synchronous write requirement
 *
 * Original address: 0x00E12E5E
 * Size: 1044 bytes
 *
 * TODO(source-bab): pmap_$write_page (0x00E12E5E, 1044 bytes) is not yet
 * decompiled; the file below has no function body at all, so both the
 * remote (NETWORK_$WRITE) and local (DISK_$WRITE) write paths, the
 * sub-page split for small packet sizes, the parity-error
 * AST_$SAVE_CLOBBERED_UID handling, the network sequence-number update and
 * every error path are missing.  It needs the same table accessors as
 * pmap_$write_complete (page frame table 0xEB4800, MMAPE 0xEC5400,
 * physical map 0xED5000) plus the partner/packet-size protocol.  Tracked
 * by bead source-bab ("Implement pmap_$write_page (1044 bytes) -
 * disk/network page write with dual paths").
 */

#include "pmap/pmap_internal.h"

/* Stub - complex 1044-byte function with disk/network write paths */
