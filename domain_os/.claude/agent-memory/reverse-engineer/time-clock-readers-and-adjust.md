---
name: time-clock-readers-and-adjust
description: TIME_ASM clock readers (TIME_$CLOCK / TIME_$ABS_CLOCK shared tail quirk, TIME_$CLOCKH_EC), TIME_$ADJUST_TIME_OF_DAY's unsigned bounds and rounding, the TIME_ data cells TIME_$ADVANCE/CANCEL use, and the host-alignment trap that breaks time/ tests
metadata:
  type: project
---

Recovered 2026-09-08 while re-emitting time/{clock,abs_clock,advance,
advance_callback,cancel,adjust_time_of_day,get_adjust,clock_to_itimer}.c.

**Why:** the earlier C for these had an unsigned 0xFE3 compare, a local copy
of the shared zero-interval cell, save/restore where the image forces IPL 0,
and missed that TIME_$ABS_CLOCK jumps into TIME_$CLOCK.

**How to apply:** trust these over the decompiler; the quirks are real.

## TIME_ASM segment (E2ADFC, size 0x484: code then data)
- TIME_$CLOCK (0xE2AFD6) and TIME_$ABS_CLOCK (0xE2B026) are hand-written:
  no link, arg via `movea.l (0x4,SP),A0`, SR saved in D1.  Kept in C (like
  TIME_$READ_CAL) so host tests can drive them via TIME_$TIMER_READ.
- ticks = `~timer + 0x1047`; `cmp.w #0xfe3 / bgt` is SIGNED.
- TIME_$ABS_CLOCK's `bgt.b 0x00e2b008` lands in TIME_$CLOCK's tail: on the
  >0xFE3 path it clamps to TIME_$CURRENT_TICK and adds TIME_$CURRENT_CLOCKL
  (0xE2B0E8) to A1 = TIME_$CLOCKH.  Only the <=0xFE3 path adds TIME_$CLOCKL
  and the constant 0x1047 (TIME_$CLOCK adds TIME_$CURRENT_TICK there).
- 0xE2B0D4 is an EC_$INIT'd ec_$eventcount_t {0, E2B0D4, E2B0D4}; the map
  marks TIME_$CLOCKH_EC and TIME_$CLOCKH at the same address, so
  TIME_$CLOCKH is that eventcount's value field (bead filed; time.h still
  models it as a bare uint32_t).

## TIME_ data segment (A5 = 0xE29198)
- (0x1608,A5) = 0xE2A7A0 TIME_$RTEQ; (0x1614,A5) = 0xE2A7AC
  time_$zero_interval, the shared all-zero one-shot interval TIME_$ADVANCE
  passes by reference.  Never a local copy.
- TIME_$CANCEL's first argument is a BY-VALUE longword (EC_$WAIT value;
  callers push `pea (0x1).w`).  EC_$WAIT takes both 3-slot arrays by value.
- TIME_$ADVANCE_CALLBACK is a direct-path callback: *arg is the element,
  elem->callback_arg (VA) is the eventcount.

## TIME_$ADJUST_TIME_OF_DAY (0xE168DE)
- |seconds| bound `cmpi.l #0x1f40 / bls` is UNSIGNED (-2^31 rejected).
- Step 0xA7 when |ticks| <= 250000 (unsigned bls), else 0x686; negated for
  negative deltas; ticks rounded toward zero with M$OIS$WLW / M$DIS$LLW /
  M$MIS$LLW.  A delta rounding to 0 clears the skew AND skips the time-of-day
  add (the rounded value is what `tst.l D2` sees).
- `ori #0x700,SR` ... `andi #0xf8ff,SR` = SET_IPL7/SET_IPL0, no restore.
  Same in TIME_$GET_ADJUST.
- usec_ticks record at A6-0x20: `clr.w` then a longword at -0x1E, so the
  32-bit tick count straddles clock_t.high/low.
- Calendar (CAL_$SEC_TO_CLOCK, ADD48, CAL_$DECODE_TIME, CAL_$WEEKDAY,
  CAL_$WRITE_CALENDAR) is rewritten on every non-error path.

## Host-test traps
- time_queue_elem_t is 0x1A bytes on m68k but sizeof() is 0x1C on the host
  (4-byte alignment).  Any `_Static_assert(sizeof(...) == 0x1A/0x1C)` on a
  record that embeds it (proc1_ts_slot_t in proc1/proc1.h did this on
  2026-09-08) breaks EVERY host test that includes time_internal.h.
- math/div.c's M$DIS$LLL is unusable on the host (bead filed); tests that
  need a signed divide must supply their own truncating one.

## Batch 2 (2026-09-19): Q_SCAN_QUEUE, GET_EC, INIT, GET_TIME_OF_DAY
- TIME_$Q_SCAN_QUEUE (0xE16E94) expires heads with expiry == now (3-word
  cmpm) OR SUB48(expiry, now) returning 0 (negative); bit1 -> ADD48 interval
  + q_insert_sorted; bit2 -> DXM_$UNWIRED_Q (0xE2ADC4), bit3 -> DXM_$WIRED_Q
  (0xE2ADE0) through DXM_$ADD_CALLBACK(q, &elem->callback, &cell, 4, bit4,
  status) with the spin lock DROPPED and re-taken; direct callbacks run WITH
  THE LOCK HELD and get &elem_cell.  The earlier C invented "unlock, call,
  goto scan_again" for both paths and had no DXM at all.
- TIME_$GET_EC caches EC2 handles in (0x1620,A5)=0xE2A7B8 and
  (0x161C,A5)=0xE2A7B4 (time_$clock_ec_handle / time_$fast_clock_ec_handle in
  time_data.c) - never function statics.  Status is tested only after BOTH
  registrations.
- TIME_$GET_TIME_OF_DAY's 1000000 rollover is `cmp.l / bmi` (signed).
- TIMER_$INIT is declared `int32_t (void)` in timer/timer.h; a host mock
  declared `void` fails with "conflicting types".
- ITIMER_REAL_INTERVAL_HIGH/LOW (0x0C/0x10) in time_internal.h are really the
  EXPIRY offsets (bead filed); TIME_$RELEASE clears the expiry, not the interval.
- Host-test ordering trap: a `time_queue_elem_t *a = make_elem(...)` initialiser
  runs BEFORE `reset()` sets ARCH_HOST_VA_BASE -> jump to 0 / SEGV 139.

## Batch 3 (2026-09-19): RELEASE, RTE_INT, SET_CPU_LIMIT, READ_CAL, itimer callbacks
- TIME_$RELEASE and both TIME_$SET_ITIMER_*_CALLBACKs touch offsets 0xC/0x10
  (and 0x664/0x668 = +0x658 virtual half) of the ITIMER_DB element: that is
  expire_high/low.  The callbacks signal only while the EXPIRY is non-zero.
  The misnamed ITIMER_*_INTERVAL_* macros are gone (source-e4a2 closed).
- Deferred-path callback arg: DXM copies the 4-byte callback_arg (as_id as a
  longword) into its entry and calls fn(&cell) where cell holds the data
  address; `movea.l (A0),A2 / move.w (0x2,A2)` is the LOW WORD of that
  longword.  Portable C: `as_id = (uint16_t)**arg`.
- TIME_$SET_CPU_LIMIT (0xE58F64) and TIME_$READ_CAL (0xE2ADFC body, 0xE2AF5E
  stub) were walked and left as committed; READ_CAL is hand-written asm kept
  in C (bead filed alongside source-6b8c).
- TIME_$RTE_INT's third arg to Q_SCAN_QUEUE is a status_$t frame local
  (never read); the routine ends with `movea.l #0,A0`.

## Batch time2 (2026-09-19): CAL_ and the rest of time/, timer/
- cal_$timezone_rec_t is 12 bytes {utc_delta:i16, tz_name[4], drift:clock_t};
  OS_CAL_WIRED (E7B030, 0x14): +0 TIMEZONE, +0xC LAST_VALID_TIME, +0x10
  BOOT_VOLX (word).  The old header had a bogus boot_volx member.
- LV label timezone fields: 0xE0 utc_delta, 0xE2 tz_name, 0xE6 last_valid_time
  (= copy of 0xB0 mount_time_high); modelled as cal_$label_tz_t in
  cal_internal.h over bat_$label_t.reserved_d0 (bead source-xb3b).
- CAL_$READ/WRITE_TIMEZONE use PROC1 lock 0xE; SET_BUFF flags 8 (read) / 0xB
  (write, shutdown); WRITE copies the label fields from the GLOBAL record
  after installing it; SHUTDOWN takes no lock.
- CAL_$VERIFY: the -0xE5 / max_delta compares are signed; msg_arg is passed
  through to VFMT "%a"; strings/cells at 0xE68476..0xE68567 (see verify.c).
- CAL_$WEEKDAY's jump table is an identity over 0..6 behind an UNSIGNED >= 7
  guard; M$OIS$WLW remainders are signed.
- CAL_$SEC_TO_CLOCK negates with neg.l (0x2,A0) / negx.w (A0) — 48-bit negate.
- TIME_$SET_TIME_OF_DAY: elapsed = WORD(abs.low - TIME_$CLOCKL) zero-extended;
  the earlier C used a signed int difference.
- TIME_$WAIT2 returns TRUE when the CALLER's ec fired (seq on index 0);
  sio/k_timed_break.c reads it backwards (bead filed).  0xE16650 crash cell
  is shared by WAIT and WAIT2 -> time_$c_queue_elem_in_use_crash in wait.c.
- TIME_$WRT_VT_TIMER is register-convention asm (D0 value, A2 clobbered) ->
  time/sau2/wrt_vt_timer.s, byte-identical.  TIME_$TIMER_HANDLER at 0xE2B130
  (336 B) has no Ghidra function and no transcription (bead source-lu78).
- TIMER_$INIT cells: E1639C 0x1046, E1639E 3, E163A0 2, E163A2 0xFFFF (shared
  by the VT and aux loads), E163A4 1.
