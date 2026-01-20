/*
 * AS_$ Data Definitions
 *
 * Global data for the Address Space subsystem.
 * Original addresses in Domain/OS kernel:
 * - AS_$INFO: 0xE2B914
 * - AS_$INFO_SIZE: 0xE2B970
 * - AS_$PROTECTION: 0xE2B972
 *
 * The initial values below are for M68010 systems (default).
 * AS_$INIT adjusts these values for M68020 systems.
 */

#include "as/as.h"

/*
 * AS_$INFO - Main address space info structure
 *
 * Initial values from ROM dump at 0xE2B914:
 * 00e2b914  00 02 00 00 00 9c 00 00  00 30 00 00 00 9c 00 00
 * 00e2b924  00 30 00 00 00 00 80 00  00 94 00 00 00 99 00 00
 * 00e2b934  00 00 80 00 00 99 80 00  00 02 80 00 00 94 00 00
 * 00e2b944  00 00 80 00 00 94 80 00  00 04 00 00 00 98 80 00
 * 00e2b954  00 04 80 00 00 98 80 00  00 00 80 00 00 04 80 00
 * 00e2b964  00 99 80 00 ...
 */
as_$info_t AS_$INFO = {
    .reserved_00 = 0x0002,
    .reserved_02 = 0x0000,
    .global_a = 0x009C0000,           /* Global A base (M68010) */
    .global_a_size = 0x00300000,      /* Global A size: 3MB */
    .m68020_global_a = 0x009C0000,    /* M68020 copy (updated in INIT) */
    .m68020_global_a_size = 0x00300000,
    .private_base = 0x00008000,       /* Private region base */
    .stack_file_low = 0x00940000,     /* Stack file low */
    .cr_rec = 0x00990000,             /* CR record address */
    .reserved_20 = 0x00008000,
    .cr_rec_end = 0x00998000,         /* CR record end */
    .reserved_28 = 0x00028000,
    .stack_file_high = 0x00940000,    /* Stack file high */
    .reserved_30 = 0x00008000,
    .stack_low = 0x00948000,          /* Stack low */
    .reserved_38 = 0x00040000,
    .stack_high = 0x00988000,         /* Stack high */
    .reserved_40 = 0x00048000,
    .stack_offset = 0x00988000,       /* Stack offset */
    .reserved_48 = 0x00008000,
    .init_stack_file_size = 0x00048000, /* Initial stack file size */
    .cr_rec_file = 0x00998000,        /* CR record file */
    .reserved_54 = 0x00000004,
    .cr_rec_file_size = 0x00030000,   /* CR record file size */
};

/*
 * AS_$INFO_SIZE - Size of the info structure in bytes
 * At 0xE2B970: 00 5c = 92 bytes
 */
int16_t AS_$INFO_SIZE = 92;  /* 0x5C */

/*
 * AS_$PROTECTION - Protection flags
 * At 0xE2B972: 00 cc
 */
int16_t AS_$PROTECTION = 0x00CC;
