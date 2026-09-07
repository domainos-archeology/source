/*
 * svc/svc_internal.h - System Call (SVC) Subsystem Internal Definitions
 *
 * Declarations used only within svc/ (the trap dispatch tables and the
 * assembly-language error handlers in svc/sau2/).  External consumers
 * should use svc/svc.h.
 */

#ifndef SVC_INTERNAL_H
#define SVC_INTERNAL_H

#include "svc/svc.h"

/*
 * ============================================================================
 * Image addresses of the dispatch tables (SAU2, SR10.2)
 * ============================================================================
 *
 * The nine tables are packed back to back at the tail of the SVC_CATCHER
 * segment (SAU2 map: "D E7B044 SVC_CATCHER size = E40", so the segment ends
 * at 0xE7BE84, where the PROC2 segment begins).  Each address below is the
 * target of the `lea (TABLE:w,%pc)' in the corresponding svc/sau2/trapN.s,
 * and each table's element count times its element size is exactly the
 * distance to the next one - see the _Static_asserts in svc/svc_tables.c.
 *
 *   0xE7B2DE  SVC_$TRAP0_TABLE      32 * 4 = 0x080   lea at 0xE7B04C
 *   0xE7B35E  SVC_$TRAP1_TABLE      66 * 4 = 0x108   lea at 0xE7B064
 *   0xE7B466  SVC_$TRAP2_TABLE     133 * 4 = 0x214   lea at 0xE7B09C
 *   0xE7B67A  SVC_$TRAP3_TABLE     155 * 4 = 0x26C   lea at 0xE7B0E0
 *   0xE7B8E6  SVC_$TRAP4_TABLE     131 * 4 = 0x20C   lea at 0xE7B128
 *   0xE7BAF2  SVC_$TRAP5_TABLE      99 * 4 = 0x18C   lea at 0xE7B184
 *   0xE7BC7E  SVC_$TRAP7_TABLE      59 * 4 = 0x0EC   lea at 0xE7B1E0
 *   0xE7BD6A  SVC_$TRAP8_TABLE      56 * 4 = 0x0E0   lea at 0xE7B27C,
 *                                                          0xE7B28E, 0xE7B2CC
 *   0xE7BE4A  SVC_$TRAP8_ARGCOUNT   56 * 1 = 0x038   lea at 0xE7B240
 *   0xE7BE82  (end; 2 bytes of alignment pad to 0xE7BE84)
 */
#define SVC_TRAP0_TABLE_ADDR        0x00E7B2DEu
#define SVC_TRAP1_TABLE_ADDR        0x00E7B35Eu
#define SVC_TRAP2_TABLE_ADDR        0x00E7B466u
#define SVC_TRAP3_TABLE_ADDR        0x00E7B67Au
#define SVC_TRAP4_TABLE_ADDR        0x00E7B8E6u
#define SVC_TRAP5_TABLE_ADDR        0x00E7BAF2u
#define SVC_TRAP7_TABLE_ADDR        0x00E7BC7Eu
#define SVC_TRAP8_TABLE_ADDR        0x00E7BD6Au
#define SVC_TRAP8_ARGCOUNT_ADDR     0x00E7BE4Au
#define SVC_TABLES_END_ADDR         0x00E7BE82u

/*
 * SVC_TABLE_SECTION - keep the tables next to the dispatchers.
 *
 * In the image the tables are part of the SVC_CATCHER segment, and each
 * dispatcher reaches its table with a 16-bit PC-relative displacement.  If
 * the tables are emitted as ordinary `.data' the linker puts them tens of
 * kilobytes away from svc/sau2/*.o and all 11 R_68K_PC16 relocations
 * overflow, so give them a section of their own that sau2.ld emits directly
 * after the dispatcher code (source-a5t8).
 */
#if defined(ARCH_M68K)
#define SVC_TABLE_SECTION   __attribute__((section(".text.svc_tables")))
#else
#define SVC_TABLE_SECTION
#endif

/*
 * Error handlers (assembly, svc/sau2/trap5.s)
 */
void SVC_$INVALID_SYSCALL(void);   /* Invalid syscall number */
void SVC_$UNIMPLEMENTED(void);     /* Unimplemented syscall */

#endif /* SVC_INTERNAL_H */
