/*
 * tone_internal.h - TONE Module Internal Definitions
 *
 * This header is for use ONLY within the tone/ subsystem.
 * It includes the public tone.h and adds internal data and helpers.
 */

#ifndef TONE_INTERNAL_H
#define TONE_INTERNAL_H

#include "tone/tone.h"

/* Dependencies on other subsystems */
#include "proc1/proc1.h"
#include "time/time.h"
#include "sio2681/sio2681.h"

/*
 * ============================================================================
 * Internal Data Declarations
 * ============================================================================
 *
 * The SIO2681 channel record used for tone output is initialized elsewhere:
 * TERM_$INIT hands its address to SIO2681_$INIT as the channel-A record.
 */

/* TONE_$CHANNEL (0x00E2DC58 = TERM_$DATA + 0x1268) is defined in tone/tone.h. */

/*
 * ============================================================================
 * Internal Constants (from disassembly)
 * ============================================================================
 *
 * These constants are embedded in the code at:
 *   0xE1735A: Tone enable byte (0xFF)
 *   0xE1735C: Tone disable byte (0x00)
 *   0xE1735E: Wait delay type (0x0000 = relative)
 */

/* Tone enable/disable values for TONE_$ENABLE parameter */
#define TONE_ENABLE_VALUE   0xFF    /* Bit 7 set = tone on */
#define TONE_DISABLE_VALUE  0x00    /* Bit 7 clear = tone off */

/* TIME_$WAIT delay type for relative waits */
#define TONE_WAIT_RELATIVE  0x0000

#endif /* TONE_INTERNAL_H */
