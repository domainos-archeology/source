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
 */
int8_t FLOP_$BOOT(uint32_t *entry_point, status_$t *status_ret)
{
    uid_t boot_shell_uid;
    uint8_t map_info[4];        /* Mapping info from MST_$MAP */
    uint8_t lock_info[8];       /* Lock info from FILE_$LOCK */
    uint32_t header[6];         /* Executable header (24 bytes) */
    uint8_t unmap_info[4];      /* Unmap info */
    void *mapped_addr;

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

    /* Step 3: Lock the boot shell file */
    {
        /* Lock parameters - these values come from data at 0x00E32538-0x00E32542 */
        int16_t lock_mode = 0;      /* Read lock */
        int32_t lock_start = 0;     /* From beginning */
        int32_t lock_len = 0;       /* Entire file */
        FILE_$LOCK(&boot_shell_uid, &lock_mode, &lock_start, &lock_len,
                   lock_info, status_ret);
    }
    flop_$boot_errchk(cant_lock_msg);
    if (*status_ret != status_$ok) {
        return 0;
    }

    /* Step 4: Map the file to read the header */
    {
        /* Map parameters */
        int32_t map_start = 0;      /* From beginning */
        int32_t map_len = 0;        /* Map whole file initially */
        int16_t map_mode = 0;       /* Read-only */
        int32_t extend = 0;         /* Don't extend */
        int16_t concurrency = 0;    /* Default concurrency */

        mapped_addr = MST_$MAP(&boot_shell_uid, &map_start, &map_len, &map_mode,
                               &extend, &concurrency, map_info, status_ret);
    }
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
     */
    {
        uint32_t *src = (uint32_t *)mapped_addr;
        int i;
        for (i = 0; i < 6; i++) {
            header[i] = src[i];
        }
    }

    /* Step 6: Unmap the initial mapping */
    MST_$UNMAP(&boot_shell_uid, unmap_info, map_info, status_ret);
    flop_$boot_errchk(cant_unmap_msg);
    if (*status_ret != status_$ok) {
        return 0;
    }

    /* Step 7: Map at the indicated load address from header */
    {
        int32_t map_start = 0;
        int32_t map_len = 0;
        int16_t map_mode = 0;
        int32_t extend = 0;
        int16_t concurrency = 0;

        MST_$MAP_AT(header, &boot_shell_uid, &map_start, &map_len, &map_mode,
                    &extend, &concurrency, map_info, status_ret);
    }
    flop_$boot_errchk(cant_map_at_msg);
    if (*status_ret != status_$ok) {
        return 0;
    }

    /* Step 8: Return the entry point address */
    *entry_point = header[1];  /* Entry point is second long in header */

    /* Return success: -1 if status is OK, 0 otherwise */
    return (*status_ret == status_$ok) ? -1 : 0;
}
