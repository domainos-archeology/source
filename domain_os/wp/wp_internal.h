/*
 * WP Internal Header
 *
 * This header contains internal definitions used only within the WP
 * subsystem. External code should use wp/wp.h.
 */

#ifndef WP_INTERNAL_H
#define WP_INTERNAL_H

#include "wp/wp.h"
#include "ml/ml.h"

/* WP subsystem lock ID (same as ML_LOCK_PMAP / PMAP_LOCK_ID) */
#define WP_LOCK_ID 0x14

#endif /* WP_INTERNAL_H */
