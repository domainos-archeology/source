/*
 * smd/smd.h - Screen Management Display Module
 *
 * Provides screen/display management functions for Domain/OS.
 */

#ifndef SMD_H
#define SMD_H

#include "base/base.h"

/*
 * SMD_$UNBLANK - Unblank the display
 *
 * Restores display output after screen blanking.
 * Called when user input is detected on the display.
 *
 * Original address: TBD
 */
void SMD_$UNBLANK(void);

#endif /* SMD_H */
