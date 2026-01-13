// TTY raw mode functions
//
// TTY_$I_SET_RAW - Set raw mode
// Address: 0x00e673aa
//
// TTY_$I_INQ_RAW - Inquire raw mode setting
// Address: 0x00e673ea
//
// TTY_$I_ENABLE_CRASH_FUNC - Enable/disable crash character
// Address: 0x00e67292

#include "tty.h"

// External helper function to set raw mode
extern void FUN_00e1bf70(tty_desc_t *tty, char raw);

void TTY_$I_SET_RAW(short line, char raw, status_$t *status)
{
    tty_desc_t *tty;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(line, status);
    if (*status != status_$ok) {
        return;
    }

    // Call internal raw mode setter
    FUN_00e1bf70(tty, raw);
}

void TTY_$I_INQ_RAW(short line, char *raw, status_$t *status)
{
    tty_desc_t *tty;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(line, status);
    if (*status != status_$ok) {
        return;
    }

    // Return raw mode flag
    *raw = tty->raw_mode;
}

void TTY_$I_ENABLE_CRASH_FUNC(tty_desc_t *tty, uint8_t ch, char enable)
{
    if (enable < 0) {  // true in Domain/OS convention
        // Enable crash character
        tty->crash_char = ch;
        // Set the character class to crash (0x11)
        tty->char_class[ch] = TTY_CHAR_CLASS_CRASH;
    } else {
        // Disable crash character
        if (tty->crash_char != 0) {
            // Reset the old crash char to normal class
            tty->char_class[ch] = TTY_CHAR_CLASS_NORMAL;
        }
        tty->crash_char = 0;
    }
}
