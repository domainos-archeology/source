// TTY_$I_GET_DESC - Get TTY descriptor for a terminal line
// Address: 0x00e66738
// Size: 140 bytes

#include "tty.h"

// External terminal subsystem functions
extern short TERM_$GET_REAL_LINE(short line_num, status_$t *status);
extern void TERM_$SET_DISCIPLINE(short *line_ptr, short *discipline_ptr, status_$t *status);
extern void SMD_$UNBLANK(void);

// External data
extern char DTTY_$USE_DTTY;  // Flag indicating if display TTY is active

// DTTE array base (at 0xe2dc90 + 0x24 offset for handler_ptr)
// Each DTTE is 0x38 bytes
#define DTTE_BASE    0xe2dc90
#define DTTE_STRIDE  0x38

// Handler pointer offset within DTTE
#define DTTE_HANDLER_OFFSET  0x24

// Discipline offset within DTTE
#define DTTE_DISCIPLINE_OFFSET  0x34

// TTY discipline value
static short tty_discipline = 0;  // 0 = TTY discipline

tty_desc_t *TTY_$I_GET_DESC(short line, status_$t *status)
{
    short real_line;
    m68k_ptr_t *handler_ptr_addr;
    m68k_ptr_t handler;
    short *discipline_addr;
    status_$t local_status;

    // Convert logical line to real line number
    real_line = TERM_$GET_REAL_LINE(line, status);
    if (*status != status_$ok) {
        return NULL;
    }

    // Calculate address of handler pointer in DTTE
    // DTTE[line].handler_ptr is at DTTE_BASE + line*DTTE_STRIDE + DTTE_HANDLER_OFFSET
    // But the indexing is: line * 0x38 where 0x38 = -8 + 8*8 = -8 + 64
    // Actually: offset = line * 0x38, then add base + 0x24
    int offset = (int)(short)(real_line * DTTE_STRIDE);
    handler_ptr_addr = (m68k_ptr_t *)(DTTE_BASE + offset + DTTE_HANDLER_OFFSET);
    handler = *handler_ptr_addr;

    if (handler == 0) {
        // No handler - line not implemented
        *status = status_$requested_line_or_operation_not_implemented;
        return NULL;
    }

    // Special handling for line 0 (console)
    if (real_line == 0) {
        // Check if display TTY is active
        if (DTTY_$USE_DTTY >= 0) {  // Not negative = display TTY available
            // Check discipline
            discipline_addr = (short *)(DTTE_BASE + offset + DTTE_DISCIPLINE_OFFSET);
            if (*discipline_addr != 2) {  // 2 = already using display
                // Switch to TTY discipline
                TERM_$SET_DISCIPLINE(&line, &tty_discipline, &local_status);
            }
        }
        // Unblank the screen
        SMD_$UNBLANK();
    }

    // Return the handler (TTY descriptor) pointer
    return (tty_desc_t *)(uintptr_t)handler;
}
