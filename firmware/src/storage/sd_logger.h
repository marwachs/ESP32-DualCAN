#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board/board.h"

/* CAN-to-microSD logger for boards that define BOARD_HAS_SD.
 *
 * Frames are pushed from the CAN poll path into a lock-light ring buffer
 * (PSRAM when available). A low-priority writer task drains it, formats
 * SavvyCAN "GVRET native" CSV in an internal-RAM staging buffer and writes
 * it to SD_LOG_DIR. Files rotate at a size limit; the oldest logs are
 * deleted when free space drops below a floor. Nothing here runs (or is
 * allocated) until sd_logger_start() is called.
 *
 * File format (one file per rotation step, SD_LOG_DIR/<prefix>_NNNNNN.csv):
 *   Time Stamp,ID,Extended,Dir,Bus,LEN,D1,D2,D3,D4,D5,D6,D7,D8
 *   12345678,00000118,false,Rx,0,8,01,02,03,04,05,06,07,08,
 * Time Stamp is esp_timer microseconds since boot, ID is 8-digit hex, the
 * row ends with a trailing comma exactly like SavvyCAN's own writer.
 *
 * Markers (sd_logger_mark) go to a sidecar file next to the CSV so SavvyCAN
 * can still load the frame file unchanged:
 *   SD_LOG_DIR/<prefix>_NNNNNN.mrk  ->  "Time Stamp,Label" then "<ts_us>,<label>"
 * Markers share the frame timebase. Commas/CR/LF in labels become '_'/' '.
 *
 * Numbering is global across prefixes: on start the directory is scanned and
 * the next file gets max(NNNNNN) + 1, so reboots never overwrite a log.
 * "Oldest" for the free-space guard means the lowest NNNNNN. */

#ifndef SD_LOG_DIR
#define SD_LOG_DIR                "/sdcard/logs"
#endif
#define SD_LOG_PREFIX_DEFAULT     "can"
#define SD_LOG_PREFIX_MAX         15      /* chars, [A-Za-z0-9_-] only */
#define SD_LOG_LABEL_MAX          47      /* chars per marker label */
#define SD_LOG_FILE_MAX           32      /* "<prefix>_NNNNNN.csv" + NUL */

#ifndef SD_LOG_RING_FRAMES
#define SD_LOG_RING_FRAMES        16384   /* 24 B each: 384 KB, PSRAM first */
#endif
#ifndef SD_LOG_MAX_FILE_BYTES
#define SD_LOG_MAX_FILE_BYTES     (16u * 1024u * 1024u)
#endif
#ifndef SD_LOG_MIN_FREE_BYTES
#define SD_LOG_MIN_FREE_BYTES     (256ull * 1024u * 1024u)
#endif
#ifndef SD_LOG_SYNC_MS
#define SD_LOG_SYNC_MS            2000
#endif

#define SD_LOG_FLAG_EXT           0x01
#define SD_LOG_FLAG_RTR           0x02
#define SD_LOG_FLAG_TX            0x04

typedef struct {
    bool     running;
    char     file[SD_LOG_FILE_MAX];   /* current file name (no directory) */
    char     prefix[SD_LOG_PREFIX_MAX + 1];
    uint32_t frames;            /* frames written to SD this session */
    uint32_t dropped;           /* frames lost because the ring was full */
    uint32_t marks;             /* markers written */
    uint64_t bytes;             /* CSV bytes written this session */
    uint32_t file_bytes;        /* bytes in the current file */
    uint32_t rotations;         /* file switches after the first file */
    uint32_t deleted;           /* old logs removed by the free-space guard */
    uint32_t write_errors;
    uint32_t ring_used;         /* records waiting right now */
    uint32_t ring_high_water;   /* max records waiting this session */
    uint32_t ring_size;
    uint32_t stack_free;        /* writer task stack high-water mark, bytes */
    uint32_t max_file_bytes;
    uint64_t min_free_bytes;
} sd_logger_status_t;

#if defined(BOARD_HAS_SD) && BOARD_HAS_SD

/* Set while a session is active. Read lock-free on the CAN hot path. */
extern volatile bool g_sd_logger_running;

static inline bool sd_logger_is_running(void)
{
    return g_sd_logger_running;
}

/* Start a session (mounts the card if needed, creates SD_LOG_DIR, opens the
 * next file). prefix may be NULL/"" for SD_LOG_PREFIX_DEFAULT. Blocks for the
 * directory scan / free-space cleanup. Returns true if logging (already
 * running counts as success). Call from task context only. */
bool sd_logger_start(const char *prefix);

/* Drain the ring, fsync, close the file and stop the writer task. Returns
 * true if a session was stopped cleanly or none was running. */
bool sd_logger_stop(void);

/* Queue a marker at the current timestamp. false if not running, the label
 * slots are full, or the label is empty. */
bool sd_logger_mark(const char *label);

/* Non-blocking: copies one record into the ring under a spinlock. Never
 * waits; counts a drop when the ring is full. Safe from any task (not ISR). */
void sd_logger_push(int bus, uint32_t id, uint8_t flags,
                    const uint8_t *data, uint8_t dlc);

void sd_logger_get_status(sd_logger_status_t *out);

/* Runtime limits (not persisted). min_free_bytes 0 disables the guard.
 * Takes effect at the next rotation / free-space check. */
void sd_logger_set_limits(uint32_t max_file_bytes, uint64_t min_free_bytes);

/* Console hook for `sd log ...`. Returns true if handled. */
bool sd_logger_console(const char *line, const char *lower);

#else  /* !BOARD_HAS_SD: inline stubs, no code or RAM on these boards. */

static inline bool sd_logger_is_running(void) { return false; }
static inline bool sd_logger_start(const char *prefix) { (void)prefix; return false; }
static inline bool sd_logger_stop(void) { return true; }
static inline bool sd_logger_mark(const char *label) { (void)label; return false; }
static inline void sd_logger_push(int bus, uint32_t id, uint8_t flags,
                                  const uint8_t *data, uint8_t dlc)
{
    (void)bus; (void)id; (void)flags; (void)data; (void)dlc;
}
static inline void sd_logger_get_status(sd_logger_status_t *out)
{
    if (out) *out = (sd_logger_status_t){0};
}
static inline void sd_logger_set_limits(uint32_t max_file_bytes, uint64_t min_free_bytes)
{
    (void)max_file_bytes; (void)min_free_bytes;
}
static inline bool sd_logger_console(const char *line, const char *lower)
{
    (void)line; (void)lower;
    return false;
}

#endif
