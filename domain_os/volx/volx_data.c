/*
 * VOLX subsystem data definitions
 *
 * The mount table.  In the image it is the module data segment
 * `D    E82604  VOLX_    size = C0` - six 0x20-byte entries at
 * 0xE82604..0xE826C3, addressed 1..6 through a pointer biased by one entry
 * (see volx/volx_internal.h).  The m68k build reaches that address directly,
 * so this object only backs host builds; it is defined unconditionally so the
 * declaration in volx_internal.h always resolves.
 */

#include "volx/volx_internal.h"

volx_$entry_t volx_$table_storage[VOLX_MAX_VOLUMES];
