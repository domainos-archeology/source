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
#include "peb/peb.h"

/*
 * Pointer to the XPD data area (0x00E32390), passed to MST_$WIRE_AREA
 * by XPD_$INIT.  PTR_PROC2_$DATA (0x00E3238C) comes from proc2/proc2.h.
 */
extern void *PTR_XPD_$DATA;

/*
 * FPU configuration flags tested by XPD_$GET_REGISTERS / XPD_$SET_REGISTERS
 * come from peb/peb.h (source-xoq):
 *   0x00E24C98  M68881_$SAVE_FLAG   - MC68881/68882 present (set by PEB_$INIT)
 *   0x00E24C92  PEB_$INSTALLED_FLAG - peripheral board FPU present
 * Both are Domain booleans: 0xFF is true, tested with `< 0`.
 */

/*
 * Constants in code space that the XPD functions pass by reference:
 *   0x00E5BDBE: zero byte handed to PROC2_$FIND_ASID as its flag parameter
 *   0x00E5BDC0 / 0x00E75044: word 0x0002, the response handed to
 *                            XPD_$CONTINUE_PROC when releasing a target
 */
static const int8_t xpd_find_asid_flag = 0;
static const xpd_$response_t xpd_continue_response = 2;

#endif /* XPD_INTERNAL_H */
