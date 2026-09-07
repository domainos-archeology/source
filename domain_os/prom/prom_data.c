/*
 * prom/prom_data.c - PROM subsystem global data definitions
 *
 * On the m68k the PROM cells live at fixed absolute addresses in the trap
 * page, so prom/prom.h reaches them through address macros and this file
 * defines nothing.  Host builds need real storage to link against.
 */

#include "prom/prom.h"

#if !defined(ARCH_M68K)
/* 0x00000100 - see the PROM_$MACHINE_ID comment in prom/prom.h. */
uint32_t PROM_$MACHINE_ID = 0;
#else
/* Keep the translation unit non-empty on the m68k build. */
typedef int prom_$data_translation_unit_not_empty;
#endif
