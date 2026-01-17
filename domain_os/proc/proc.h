/*
 * PROC - Process Management Module (Combined Header)
 *
 * This header provides unified access to process management functions
 * which are split across proc1 (scheduling/dispatching) and proc2
 * (process lifecycle, signals, etc.)
 */

#ifndef PROC_H
#define PROC_H

#include "proc1/proc1.h"
#include "proc2/proc2.h"

#endif /* PROC_H */
