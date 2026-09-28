/*
 * log/add.c - LOG_$ADD (0x00E1763E, 312 bytes)
 *
 * Appends a timestamped entry to the circular log page, discarding the
 * oldest entries it would overwrite, and mirrors the entry into
 * LOG_$LAST_ENTRY at 0x00E0000C.
 *
 * Frame (link.w A6,-0x20; A5 A4 A3 A2 D6 D5 D4 D3 D2 saved), A5 = 0xE2B280:
 *   (0x8,A6)   type       word
 *   (0xa,A6)   data       address of the data words
 *   (0xe,A6)   data_len   word, in BYTES
 *   A6-0x14    buf        the log page (LOG_$LOGFILE_PTR, cached)
 *   D2         words      entry size in words = data words + 4
 *   D3         data words
 *   D4         type
 *   D5         the spin-lock token
 *
 * Buffer layout (words): [0] head index, [1] tail index; entry i is at
 * buf[i + 1], so the page holds indices 1..0x1FE.
 */

#include "log/log_internal.h"

void LOG_$ADD(int16_t type, void *data, int16_t data_len)
{
    int16_t *buf;                       /* A6-0x14 */
    int16_t *entry;                     /* A1 */
    int16_t *src;                       /* A0 */
    int16_t words;                      /* D2 */
    int16_t data_words;                 /* D3 */
    int16_t last;                       /* D0: index of the entry's last word */
    int16_t size;                       /* D1 */
    ml_$spin_token_t token;             /* D5 */
    uint32_t now;                       /* D6 */
    int16_t i;

    /* 0x00E17654-0x00E17658: nothing to write to before LOG_$INIT. */
    if (LOG_$LOGFILE_PTR == NULL) {
        return;
    }

    /* 0x00E1765C-0x00E17668: (data_len + 1) div 2, rounded toward zero
     * (`ext.l` / `addq.l #1` / `bpl` / `addq.l #1` / `asr.l #1`). */
    data_words = (int16_t)(((int32_t)data_len + 1) / 2);
    words = (int16_t)(data_words + 4);

    /* 0x00E1766C-0x00E17676: 1..100 words, else silently dropped. */
    if (words <= 0 || words > LOG_MAX_ENTRY_WORDS) {
        return;
    }

    /* 0x00E1767A-0x00E1768C */
    LOG_$STATE.dirty_flag = 0;
    token = ML_$SPIN_LOCK(&LOG_$STATE.spin_lock);

    /* 0x00E1768E-0x00E176B8: the entry would end past index 0x1FE -> mark
     * the slot at the current tail with a zero size (the end-of-page
     * sentinel the walk below stops on), wrap both indices to 1, and the
     * entry now ends at index `words`. */
    buf = LOG_$LOGFILE_PTR;
    last = (int16_t)(words + buf[1] - 1);
    if (last > LOG_MAX_INDEX) {
        buf[buf[1] + 1] = 0;
        buf[0] = 1;
        buf[1] = 1;
        last = words;
    }

    /* 0x00E176BC-0x00E176E0: while the head has not fallen behind the tail
     * and still lies within the new entry, advance it over the entry it
     * points at; a zero size (the sentinel) or running off the page wraps
     * it to 1.  Quirk reproduced: after a wrap both indices are 1, so a
     * zero size word at index 1 (a page that was never written past its
     * first entry) wraps the head to 1 again without end. */
    while (!(buf[0] < buf[1]) && buf[0] <= last) {
        size = buf[buf[0] + 1];
        if (size == 0) {
            buf[0] = 1;
        } else {
            buf[0] = (int16_t)(buf[0] + size);
            if (!(buf[0] < LOG_MAX_INDEX)) {
                buf[0] = 1;
            }
        }
    }

    /* 0x00E176E2-0x00E17712: the entry header, written to the page and to
     * LOG_$LAST_ENTRY in step. */
    entry = &buf[buf[1] + 1];
    LOG_$STATE.current_entry_ptr = entry;
    entry[0] = words;
    LOG_$LAST_ENTRY.size = words;
    entry[1] = type;
    LOG_$LAST_ENTRY.type = type;
    now = TIME_$CURRENT_CLOCKH;
    ((log_entry_header_t *)entry)->timestamp = now;
    LOG_$LAST_ENTRY.timestamp = now;

    /* 0x00E17716-0x00E17742: `dbf` over data_words words (skipped when
     * data_words is 0), each to both destinations. */
    src = (int16_t *)data;
    for (i = 0; i < data_words; i++) {
        entry[4 + i] = src[i];
        LOG_$LAST_ENTRY.data[i] = src[i];
    }

    /* 0x00E17746-0x00E17756: advance the tail, wrapping past 0x1FE. */
    buf[1] = (int16_t)(buf[1] + words);
    if (buf[1] > LOG_MAX_INDEX) {
        buf[1] = 1;
    }

    /* 0x00E1775C-0x00E1776A */
    ML_$SPIN_UNLOCK(&LOG_$STATE.spin_lock, token);
    LOG_$STATE.dirty_flag = -1;
}
