/*
 * XPD Internal - Process Debugger Support Internal Definitions
 *
 * This header contains internal definitions used within the xpd subsystem.
 * External code should use xpd.h instead.
 */

#ifndef XPD_INTERNAL_H
#define XPD_INTERNAL_H

#include "xpd/xpd.h"
#include "fim/fim.h"
#include "os/os.h"

/*
 * Pointer to the XPD data area (0x00E32390), passed to MST_$WIRE_AREA
 * by XPD_$INIT.  PTR_PROC2_$DATA (0x00E3238C) comes from proc2/proc2.h.
 */
extern void *PTR_XPD_$DATA;

/*
 * FPU configuration flags tested by XPD_$GET_REGISTERS / XPD_$SET_REGISTERS.
 *   DAT_00e24c98: MC68881/68882 present (M68881_$SAVE_FLAG, set by PEB_$INIT)
 *   DAT_00e24c92: peripheral board FPU present (PEB_$INSTALLED)
 * TODO: these live in the PEB globals block (peb/peb_internal.h names them
 * as fields of PEB_GLOBALS); declare them through peb/peb.h once that
 * header exports the block.
 */
extern char DAT_00e24c98;
extern char DAT_00e24c92;

/*
 * Constants in code space that the XPD functions pass by reference:
 *   0x00E5BDBE: zero byte handed to PROC2_$FIND_ASID as its flag parameter
 *   0x00E5BDC0 / 0x00E75044: word 0x0002, the response handed to
 *                            XPD_$CONTINUE_PROC when releasing a target
 */
static const int8_t xpd_find_asid_flag = 0;
static const xpd_$response_t xpd_continue_response = 2;

#endif /* XPD_INTERNAL_H */
