/*
 * VOLX subsystem data definitions
 *
 * The mount table.  In the image it is the module data segment
 * `D    E82604  VOLX_    size = C0` - six 0x20-byte entries at
 * 0xE82604..0xE826C3, addressed 1..6 through a pointer biased by one entry
 * (see volx/volx_internal.h).  A MODULE_DATA block on every build
 * (source-702z); zero in the image (`gsk read 0xE82604 0xC0`).
 */

#include "volx/volx_internal.h"

MODULE_DATA_DEFINE(volx_$table_t, VOLX_$DATA, 0x00E82604);

/*
 * The zero longword at 0x00E6B504 that both VOLX_$DISMOUNT (0x00E6B4A8) and
 * VOLX_$SHUTDOWN (0x00E6B548) pass as DIR_$DROP_MOUNT's lv_num argument.
 * One cell in the image, so one object here.
 */
uint32_t volx_$drop_mount_lv = 0;
