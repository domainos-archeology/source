/*
 * flop/boot.c - FLOP_$BOOT implementation
 *
 * Boots the system from a floppy disk.
 *
 * Original address: 0x00E3254C (338 bytes)
 *
 * Every constant this routine hands a callee is a cell the compiler placed in
 * the FLOP_ code segment and reached with `pea (d,PC)`; the values below are
 * the image bytes (`gsk read`), and the address of each cell is given so the
 * sharing is visible - 0x00E32538 in particular is ONE byte that serves as
 * both FILE_$LOCK's `rights` and MST_$MAP_AT's `concurrency`, and 0x00E32730
 * is ONE longword passed twice in each mapping call.  (source-y89n)
 */

#include "flop/flop_internal.h"

/*
 * ----------------------------------------------------------------------------
 * Message strings
 *
 * All five end with '%' then NUL - the boot console's end-of-line convention -
 * not with a full stop.
 * ----------------------------------------------------------------------------
 */

/* 0x00E3269E: "bad floppy mount%" (pea (0x13e,PC) at 0x00E3255E) */
static const char flop_bad_floppy_mount_msg[] = "bad floppy mount%";

/* 0x00E326C8: "can't lock boot shell%" (pea (0x112,PC) at 0x00E325B4) */
static const char flop_cant_lock_msg[] = "can't lock boot shell%";

/* 0x00E326B0: "can't map boot shell%" (pea (0xbc,PC) at 0x00E325F2) */
static const char flop_cant_map_msg[] = "can't map boot shell%";

/* 0x00E32714: "can't unmap boot shell%" (pea (0xde,PC) at 0x00E32634) */
static const char flop_cant_unmap_msg[] = "can't unmap boot shell%";

/* 0x00E326F4: "can't map at indicated address%" (pea (0x80,PC) at 0x00E32672) */
static const char flop_cant_map_at_msg[] = "can't map at indicated address%";

/*
 * ----------------------------------------------------------------------------
 * The boot shell's path
 *
 * 0x00E326DE holds the length word 0x0013 = 19 and 0x00E326E0 the 19
 * characters; NAME_$RESOLVE gets both by address (pea (0x162,PC) and
 * pea (0x164,PC) at 0x00E3257C / 0x00E32578).
 * ----------------------------------------------------------------------------
 */
static char flop_boot_shell_path[] = "/flp/sys/boot_shell";
static int16_t flop_boot_shell_path_len = 0x0013;

/*
 * ----------------------------------------------------------------------------
 * The `pea (d,PC)` constant cells, with their image bytes
 * ----------------------------------------------------------------------------
 */

/*
 * 0x00E32538: 00 00 - a zero byte.
 *
 * FILE_$LOCK reads it as `rights` (pea (-0x64,PC) at 0x00E3259A) and
 * MST_$MAP_AT reads it as `concurrency` (pea (-0x116,PC) at 0x00E3264C -
 * 0x00E3264E - 0x116 = 0x00E32538).  It is NOT the 0xFF cell at 0x00E32542
 * that MST_$MAP gets.  (source-y89n)
 */
static uint8_t flop_zero_byte = 0x00;

/* 0x00E3253A: 00 04 - FILE_$LOCK's lock mode (pea (-0x66,PC) at 0x00E3259E) */
static uint16_t flop_lock_mode = 0x0004;

/* 0x00E32540: 00 01 - FILE_$LOCK's lock index (pea (-0x64,PC) at 0x00E325A2) */
static uint16_t flop_lock_index = 0x0001;

/* 0x00E32542: ff - MST_$MAP's concurrency (pea (-0x8e,PC) at 0x00E325CE) */
static uint8_t flop_map_concurrency = 0xFF;

/* 0x00E326C6: 00 07 - the mapping mode word, wedged between two strings
 * (pea (0xee,PC) at 0x00E325D6 and pea (0x70,PC) at 0x00E32654) */
static uint16_t flop_map_mode = 0x0007;

/* 0x00E3272C: 00 10 00 00 - the mapping length, 1 MB
 * (pea (0x150,PC) at 0x00E325DA and pea (0xd2,PC) at 0x00E32658) */
static uint32_t flop_map_length = 0x00100000;

/*
 * 0x00E32730: 00 00 00 00 - ONE longword, passed twice in each mapping call
 * as both `start` and `extend`:
 *   MST_$MAP    pea (0x15c,PC) at 0x00E325D2 and pea (0x150,PC) at 0x00E325DE
 *   MST_$MAP_AT pea (0xde,PC)  at 0x00E32650 and pea (0xd2,PC)  at 0x00E3265C
 */
static uint32_t flop_map_start = 0;

/* 0x00E3260C `moveq #0x5,D0` + `move.l (A1)+,(A3)+` + `dbf`: 6 longwords. */
#define FLOP_BOOT_HEADER_LONGS  6

/*
 * FLOP_$BOOT - Boot from floppy disk
 *
 * Attempts to boot from a floppy disk by loading and mapping the boot shell
 * executable from /flp/sys/boot_shell.
 *
 * Parameters:
 *   entry_point - Output: receives header[1], the entry point address
 *   status_ret  - Status return (a var parameter, re-read after every step)
 *
 * Returns:
 *   0x00E32690-0x00E32692 `tst.l (A0)` / `seq D0b`: -1 when the final status
 *   is zero, 0 otherwise.  The image has no early `rts` - every failure
 *   branches to that same tail.
 *
 * Stack frame (link.w A6,-0x34; A6 displacements taken from the call sites):
 *   A6-0x34  4 bytes  unused (the frame is four bytes larger than the locals)
 *   A6-0x30  4 bytes  map_info, shared by MST_$MAP / MST_$UNMAP / MST_$MAP_AT
 *   A6-0x2C  8 bytes  lock_info, FILE_$LOCK's output
 *   A6-0x24  4 bytes  mapped_va, the pointer MST_$MAP returned in A0
 *   A6-0x20  8 bytes  boot_shell_uid
 *   A6-0x18 24 bytes  header, six longwords copied out of the mapped file
 */
int8_t FLOP_$BOOT(uint32_t *entry_point, status_$t *status_ret)
{
    uid_t boot_shell_uid;               /* A6-0x20 */
    uint32_t mapped_va;                 /* A6-0x24 */
    uint8_t lock_info[8];               /* A6-0x2C */
    uint32_t map_info;                  /* A6-0x30 */
    uint32_t header[FLOP_BOOT_HEADER_LONGS];    /* A6-0x18 */
    void *mapped_addr;                  /* A2 */
    int i;

    /* 0x00E32554-0x00E3255C: mount the floppy volume at /flp. */
    flop_$mount_floppy(status_ret);

    /* 0x00E3255E-0x00E32566: the error check runs unconditionally; it is the
     * routine that decides whether the message is printed. */
    flop_$boot_errchk(flop_bad_floppy_mount_msg);
    if (*status_ret != status_$ok) {            /* 0x00E32568-0x00E3256E */
        goto done;
    }

    /* 0x00E32572-0x00E32586 */
    NAME_$RESOLVE(flop_boot_shell_path, &flop_boot_shell_path_len,
                  &boot_shell_uid, status_ret);
    if (*status_ret != status_$ok) {            /* 0x00E3258A-0x00E32590 */
        goto done;
    }

    /* 0x00E32594-0x00E325B0 */
    FILE_$LOCK(&boot_shell_uid, &flop_lock_index, &flop_lock_mode,
               &flop_zero_byte, lock_info, status_ret);
    flop_$boot_errchk(flop_cant_lock_msg);      /* 0x00E325B4 */
    if (*status_ret != status_$ok) {            /* 0x00E325BE-0x00E325C4 */
        goto done;
    }

    /*
     * 0x00E325C8-0x00E325F0: map the file to read its header.  `start` and
     * `extend` are the same 0x00E32730 cell; the concurrency byte here is the
     * 0xFF one.  The mapped address comes back in A0.
     */
    mapped_addr = MST_$MAP(&boot_shell_uid, &flop_map_start, &flop_map_length,
                           &flop_map_mode, &flop_map_start,
                           &flop_map_concurrency, &map_info, status_ret);
    flop_$boot_errchk(flop_cant_map_msg);       /* 0x00E325F2 */
    if (*status_ret != status_$ok) {            /* 0x00E325FC-0x00E32602 */
        goto done;
    }

    /* 0x00E32606-0x00E32616: copy six longwords of header, then remember the
     * mapped address in the cell MST_$UNMAP will read it back out of. */
    for (i = 0; i < FLOP_BOOT_HEADER_LONGS; i++) {
        header[i] = ((uint32_t *)mapped_addr)[i];
    }
    mapped_va = ARCH_PTR_TO_VA(mapped_addr);

    /* 0x00E3261A-0x00E32630 */
    MST_$UNMAP(&boot_shell_uid, &mapped_va, &map_info, status_ret);
    flop_$boot_errchk(flop_cant_unmap_msg);     /* 0x00E32634 */
    if (*status_ret != status_$ok) {            /* 0x00E3263E-0x00E32644 */
        goto done;
    }

    /*
     * 0x00E32646-0x00E3266E: map the file again at the address its header
     * asks for.  The first argument is the header buffer itself
     * (`pea (-0x18,A6)`), from which MST_$MAP_AT reads one longword; the
     * concurrency argument is the 0x00 byte at 0x00E32538, not the 0xFF one.
     * `start` and `extend` are again the same 0x00E32730 cell.
     */
    MST_$MAP_AT(header, &boot_shell_uid, &flop_map_start, &flop_map_length,
                &flop_map_mode, &flop_map_start, &flop_zero_byte,
                &map_info, status_ret);
    flop_$boot_errchk(flop_cant_map_at_msg);    /* 0x00E32672 */
    if (*status_ret != status_$ok) {            /* 0x00E3267C-0x00E32682 */
        goto done;
    }

    /* 0x00E32684-0x00E3268A: `move.l (-0x14,A6),(A1)` - the second longword
     * of the header. */
    *entry_point = header[1];

done:
    /* 0x00E3268C-0x00E32692 */
    return (*status_ret == status_$ok) ? -1 : 0;
}
