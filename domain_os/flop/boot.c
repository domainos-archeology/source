/*
 * flop/boot.c - FLOP_$BOOT implementation
 *
 * Boots the system from a floppy disk.
 *
 * Original address: 0x00E3254C (338 bytes)
 */

#include "flop/flop_internal.h"

/*
 * String constants used by boot process
 * These are located in the data section following the code
 */
static const char bad_floppy_mount_msg[] = "bad floppy mount.";
static const char cant_lock_msg[] = "can't lock boot shell.";
static const char cant_map_msg[] = "can't map boot shell.";
static const char cant_unmap_msg[] = "can't unmap boot shell.";
static const char cant_map_at_msg[] = "can't map at indicated address.";

/*
 * Constants used by FILE_$LOCK and MST_$MAP
 * These are ROM constants in the original at 0x00E32538-0x00E32542
 *
 * FILE_$LOCK constants:
 *   0x00e32538: rights = 0x00 (no special rights)
 *   0x00e3253a: lock_mode = 0x0004
 *   0x00e32540: lock_index = 0x0001
 *
 * MST_$MAP constants:
 *   0x00e32730: start = 0
 *   0x00e3272c: length = 0x00100000 (1MB max)
 *   0x00e326c6: mode = 0x0007
 *   0x00e32542: concurrency = 0xff
 */
static const uint8_t  LOCK_RIGHTS = 0x00;
static const uint16_t LOCK_MODE = 0x0004;
static const uint16_t LOCK_INDEX = 0x0001;

static const uint32_t MAP_START = 0;
static const uint32_t MAP_LENGTH = 0x00100000;  /* 1 MB */
static const uint16_t MAP_MODE = 0x0007;
static const uint8_t  MAP_CONCURRENCY = 0xff;

/*
 * FLOP_$BOOT - Boot from floppy disk
 *
 * Attempts to boot from a floppy disk by loading and mapping
 * the boot shell executable from /flp/sys/boot_shell.
 *
 * The boot process:
 * 1. Mount the floppy volume at /flp (via flop_$mount_floppy)
 * 2. Resolve the path /flp/sys/boot_shell
 * 3. Lock the boot shell file
 * 4. Map the file into memory to read the header
 * 5. Copy the header (6 longs = 24 bytes) to get load address info
 * 6. Unmap the initial mapping
 * 7. Map the file at its indicated load address
 * 8. Return the entry point address
 *
 * Parameters:
 *   entry_point - Output: receives the entry point address
 *   status_ret  - Status return
 *
 * Returns:
 *   -1 (true) if boot successful and *entry_point is valid
 *   0 (false) if boot failed and *status_ret contains error
 *
 * Assembly (0x00E3254C):
 *   link.w  A6,#-0x34
 *   movem.l {A3 A2},-(SP)
 *   move.l  (0xc,A6),-(SP)           ; Push status_ret
 *   bsr.w   flop_$mount_floppy       ; Mount floppy
 *   addq.w  #4,SP
 *   pea     bad_floppy_mount_msg
 *   bsr.w   flop_$boot_errchk        ; Check for error
 *   ...
 *   seq     D0                       ; D0 = (*status_ret == OK) ? -1 : 0
 *   movem.l (-0x3c,A6),{A2 A3}
 *   unlk    A6
 *   rts
 *
 * Stack frame layout (A6-relative):
 *   -0x34 to -0x31: map_info (4 bytes) - output from MST_$MAP
 *   -0x30 to -0x29: lock_info (8 bytes) - output from FILE_$LOCK
 *   -0x28 to -0x25: mapped_va (4 bytes) - saved mapped address for MST_$UNMAP
 *   -0x24 to -0x1d: boot_shell_uid (8 bytes)
 *   -0x1c to -0x01: header (24 bytes = 6 longs)
 */
int8_t FLOP_$BOOT(uint32_t *entry_point, status_$t *status_ret)
{
    uid_t boot_shell_uid;
    uint32_t map_info;          /* Mapping info from MST_$MAP (4 bytes) */
    uint8_t lock_info[8];       /* Lock info from FILE_$LOCK (8 bytes) */
    uint32_t header[6];         /* Executable header (24 bytes) */
    uint32_t mapped_va;         /* Saved mapped address for MST_$UNMAP */
    void *mapped_addr;

    /* Local copies of constants (to match ROM constant behavior) */
    uint16_t lock_index = LOCK_INDEX;
    uint16_t lock_mode = LOCK_MODE;
    uint8_t rights = LOCK_RIGHTS;
    uint32_t map_start = MAP_START;
    uint32_t map_length = MAP_LENGTH;
    uint16_t map_mode = MAP_MODE;
    uint8_t concurrency = MAP_CONCURRENCY;

    /* Step 1: Mount the floppy volume */
    flop_$mount_floppy(status_ret);
    flop_$boot_errchk(bad_floppy_mount_msg);
    if (*status_ret != status_$ok) {
        return 0;
    }

    /* Step 2: Resolve the boot shell path */
    {
        static const char boot_shell_path[] = "/flp/sys/boot_shell";
        int16_t path_len = sizeof(boot_shell_path) - 1;
        NAME_$RESOLVE(boot_shell_path, &path_len, &boot_shell_uid, status_ret);
    }
    if (*status_ret != status_$ok) {
        return 0;
    }

    /* Step 3: Lock the boot shell file
     *
     * Assembly at 0x00E325AA calls FILE_$LOCK with:
     *   param 1: &boot_shell_uid
     *   param 2: &lock_index (0x0001)
     *   param 3: &lock_mode (0x0004)
     *   param 4: &rights (0x00)
     *   param 5: lock_info buffer
     *   param 6: status_ret
     */
    FILE_$LOCK(&boot_shell_uid, &lock_index, &lock_mode, &rights,
               lock_info, status_ret);
    flop_$boot_errchk(cant_lock_msg);
    if (*status_ret != status_$ok) {
        return 0;
    }

    /* Step 4: Map the file to read the header
     *
     * Assembly at 0x00E325E6 calls MST_$MAP with:
     *   param 1: &boot_shell_uid
     *   param 2: &map_start (0)
     *   param 3: &map_length (0x100000)
     *   param 4: &map_mode (0x0007)
     *   param 5: &map_start (0) - reuses same addr for extend
     *   param 6: &concurrency (0xff)
     *   param 7: &map_info (output buffer)
     *   param 8: status_ret
     *
     * Returns mapped address in A0.
     */
    mapped_addr = MST_$MAP(&boot_shell_uid, &map_start, &map_length, &map_mode,
                           &map_start, &concurrency, &map_info, status_ret);
    flop_$boot_errchk(cant_map_msg);
    if (*status_ret != status_$ok) {
        return 0;
    }

    /* Step 5: Copy the executable header (6 longs = 24 bytes)
     * The header contains:
     *   [0] = magic/format
     *   [1] = entry point address
     *   [2] = text size
     *   [3] = data size
     *   [4] = bss size
     *   [5] = symbol table size
     *
     * Assembly at 0x00E32606-0x00E32616:
     *   lea     (A2),A1              ; A1 = mapped_addr
     *   lea     (-0x18,A6),A3        ; A3 = header buffer
     *   moveq   #5,D0                ; Copy 6 longs
     *   move.l  (A1)+,(A3)+
     *   dbf     D0,loop
     *   move.l  A2,(-0x24,A6)        ; Save mapped addr to mapped_va
     */
    {
        uint32_t *src = (uint32_t *)mapped_addr;
        int i;
        for (i = 0; i < 6; i++) {
            header[i] = src[i];
        }
    }
    mapped_va = (uint32_t)(uintptr_t)mapped_addr;

    /* Step 6: Unmap the initial mapping
     *
     * Assembly at 0x00E3262A calls MST_$UNMAP with:
     *   param 1: &boot_shell_uid
     *   param 2: &mapped_va (pointer to the mapped address)
     *   param 3: &map_info (info from MST_$MAP)
     *   param 4: status_ret
     */
    MST_$UNMAP(&boot_shell_uid, &mapped_va, &map_info, status_ret);
    flop_$boot_errchk(cant_unmap_msg);
    if (*status_ret != status_$ok) {
        return 0;
    }

    /* Step 7: Map at the indicated load address from header
     *
     * Assembly at 0x00E32668 calls MST_$MAP_AT with:
     *   param 1: header (load address info)
     *   param 2: &boot_shell_uid
     *   param 3-8: same mapping parameters
     *   param 9: status_ret
     */
    {
        uint32_t extend = MAP_START;  /* Fresh copy */
        map_start = MAP_START;
        map_length = MAP_LENGTH;
        map_mode = MAP_MODE;
        concurrency = MAP_CONCURRENCY;

        MST_$MAP_AT(header, &boot_shell_uid, &map_start, &map_length, &map_mode,
                    &extend, &concurrency, &map_info, status_ret);
    }
    flop_$boot_errchk(cant_map_at_msg);
    if (*status_ret != status_$ok) {
        return 0;
    }

    /* Step 8: Return the entry point address */
    *entry_point = header[1];  /* Entry point is second long in header */

    /* Return success: -1 if status is OK, 0 otherwise
     * Assembly: seq D0 (set to -1 if zero flag set, i.e., status == 0)
     */
    return (*status_ret == status_$ok) ? -1 : 0;
}
