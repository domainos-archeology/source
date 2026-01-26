/*
 * SLINK - Symbolic Link Support
 *
 * This module provides symbolic link type identification.
 */

#ifndef SLINK_H
#define SLINK_H

#include "base/base.h"

/*
 * SLINK_$UID - Well-known UID for symbolic link type
 *
 * This UID identifies objects that are symbolic links.
 * Used to prevent certain operations on symbolic links
 * (e.g., changing type to/from symbolic link).
 */
extern uid_t SLINK_$UID;

#endif /* SLINK_H */
