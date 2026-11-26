#include "storage/sd_logger.h"

#if defined(BOARD_HAS_SD) && BOARD_HAS_SD

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "board/ext_ram.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "storage/sd.h"

static const char *TAG = "sd_log";

/* Writer task. Low priority, off the CAN core on dual-core parts: the app
 * task (CAN poll, Berry) runs on core 1, NimBLE preempts us on core 0.
 * Its stack goes to PSRAM when the build allows it (internal RAM is the
 * scarce resource with BLE up). That is safe here: the task never touches
 * SPI flash, and everything it hands to the SD card is copied by FatFs or
 * comes from the internal staging buffer. Falls back to internal RAM. */
#define SD_LOG_TASK_STACK       8192    /* PSRAM; 5120 left only ~1.2 KB free */
#define SD_LOG_TASK_PRIO        1
#define SD_LOG_TASK_CORE        0       /* app task is on core 1 (main.c) */

/* CSV staging buffer in internal DMA-capable RAM: SDSPI must not DMA from
 * PSRAM. The FILE is unbuffered so fwrite() hands this buffer straight to
 * FatFs instead of copying through a malloc'd (PSRAM) stdio buffer. */
#define SD_LOG_STAGE_SIZE       (4 * 1024)  /* = `sd test` chunk, ~600 KB/s */
#define SD_LOG_LINE_MAX         80      /* longest CSV row is 68 chars */
#define SD_LOG_DRAIN_MAX        1024    /* records per writer pass */
#define SD_LOG_RING_FALLBACK    1024    /* internal-RAM ring if no PSRAM */
#define SD_LOG_MARK_SLOTS       8
#define SD_LOG_HARD_MIN_FREE    (4ull * 1024 * 1024)
#define SD_LOG_STOP_TIMEOUT_MS  10000
#define SD_LOG_REOPEN_MS        2000
#define SD_LOG_OLDEST_BATCH     8

#define SD_LOG_CSV_HEADER       "Time Stamp,ID,Extended,Dir,Bus,LEN,D1,D2,D3,D4,D5,D6,D7,D8\n"
#define SD_LOG_MRK_HEADER       "Time Stamp,Label\n"

enum { REC_FRAME = 0, REC_MARK = 1 };

typedef struct {
    int64_t  ts_us;
    uint32_t id;        /* CAN ID, or marker slot for REC_MARK */
    uint8_t  bus;
    uint8_t  dlc;
    uint8_t  flags;     /* SD_LOG_FLAG_* */
    uint8_t  kind;      /* REC_* */
    uint8_t  data[8];
} log_rec_t;

_Static_assert(sizeof(log_rec_t) == 24, "log_rec_t should stay 24 bytes");

typedef struct {
    uint32_t seq;
    char     name[SD_LOG_FILE_MAX];
} log_file_t;

volatile bool g_sd_logger_running;

/* Everything the producer touches, plus the stats snapshot, is guarded by
 * s_mux. The writer is the only consumer, so it reads ring slots outside the
 * lock and only publishes the new tail under it. */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static log_rec_t *s_ring;
static uint32_t s_ring_size;
static uint32_t s_head, s_tail, s_count;
DORKY_EXT_BSS static char s_mark_label[SD_LOG_MARK_SLOTS][SD_LOG_LABEL_MAX + 1];
static bool s_mark_used[SD_LOG_MARK_SLOTS];
static sd_logger_status_t s_st;

static TaskHandle_t s_task;
static bool s_task_ext_stack;
static SemaphoreHandle_t s_done;
static StaticSemaphore_t s_done_buf;
static volatile bool s_stop_req;

/* Writer-task-only state (and start/stop while the task is not running). */
static FILE *s_fp;
static FILE *s_mrk_fp;
static char *s_stage;
static size_t s_stage_len;
static uint32_t s_stage_frames;
static uint32_t s_seq;
static uint32_t s_file_bytes;
static uint32_t s_max_file_bytes = SD_LOG_MAX_FILE_BYTES;
static uint64_t s_min_free_bytes = SD_LOG_MIN_FREE_BYTES;
static char s_prefix[SD_LOG_PREFIX_MAX + 1] = SD_LOG_PREFIX_DEFAULT;
static char s_file[SD_LOG_FILE_MAX];

/* ---- file names ---------------------------------------------------- */

static bool prefix_valid(const char *p)
{
    size_t n = strlen(p);
    if (n == 0 || n > SD_LOG_PREFIX_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        if (!isalnum(c) && c != '_' && c != '-') return false;
    }
    return true;
}

/* "<anything>_<digits>.csv" -> digits, 0 if the name does not match. */
static uint32_t parse_seq(const char *name)
{
    const char *us = strrchr(name, '_');
    const char *dot = strrchr(name, '.');
    if (!us || !dot || dot < us + 2 || strcasecmp(dot, ".csv") != 0) return 0;
    uint32_t seq = 0;
    for (const char *p = us + 1; p < dot; p++) {
        if (*p < '0' || *p > '9') return 0;
        if (seq > (UINT32_MAX - 9) / 10) return 0;
        seq = seq * 10 + (uint32_t)(*p - '0');
    }
    return seq;
}

static void join_path(char *out, size_t out_size, const char *name)
{
    snprintf(out, out_size, "%s/%s", SD_LOG_DIR, name);
}

/* Sidecar marker path: same name with ".mrk" instead of ".csv". */
static void mrk_path(char *out, size_t out_size, const char *csv_name)
{
    join_path(out, out_size, csv_name);
    size_t n = strlen(out);
    if (n >= 4) memcpy(out + n - 4, ".mrk", 4);
}

/* One pass over SD_LOG_DIR: highest sequence number, and (optionally) the
 * `want` lowest-numbered logs other than `exclude`, sorted ascending.
 * Returns the number of entries written to `oldest`. */
static int scan_logs(uint32_t *max_seq, log_file_t *oldest, int want, uint32_t exclude)
{
    if (max_seq) *max_seq = 0;
    DIR *dir = opendir(SD_LOG_DIR);
    if (!dir) return 0;
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        uint32_t seq = parse_seq(ent->d_name);
        if (seq == 0) continue;
        if (max_seq && seq > *max_seq) *max_seq = seq;
        if (!oldest || want <= 0 || seq == exclude) continue;
        if (strlen(ent->d_name) >= SD_LOG_FILE_MAX) continue;
        if (n == want && seq >= oldest[n - 1].seq) continue;
        int pos = (n < want) ? n++ : n - 1;
        while (pos > 0 && oldest[pos - 1].seq > seq) {
            oldest[pos] = oldest[pos - 1];
            pos--;
        }
        oldest[pos].seq = seq;
        strlcpy(oldest[pos].name, ent->d_name, sizeof(oldest[pos].name));
    }
    closedir(dir);
    return n;
}

static bool free_bytes(uint64_t *out)
{
    uint64_t total = 0;
    return esp_vfs_fat_info(SD_MOUNT_POINT, &total, out) == ESP_OK;
}

/* Delete the oldest logs (never `exclude`) until free space is back above
 * s_min_free_bytes. Returns false if the card is still nearly full. */
static bool ensure_space(uint32_t exclude)
{
    uint64_t free_b = 0;
    if (!free_bytes(&free_b)) return true;   /* unknown: do not block logging */
    log_file_t oldest[SD_LOG_OLDEST_BATCH];
    while (free_b < s_min_free_bytes) {
        int n = scan_logs(NULL, oldest, SD_LOG_OLDEST_BATCH, exclude);
        if (n == 0) break;
        for (int i = 0; i < n && free_b < s_min_free_bytes; i++) {
            char path[80];
            join_path(path, sizeof(path), oldest[i].name);
            if (unlink(path) != 0) {
                ESP_LOGW(TAG, "cannot delete %s (errno %d)", path, errno);
                return free_b >= SD_LOG_HARD_MIN_FREE;
            }
            mrk_path(path, sizeof(path), oldest[i].name);
            (void)unlink(path);
            ESP_LOGI(TAG, "free-space guard: deleted %s", oldest[i].name);
            portENTER_CRITICAL(&s_mux);
            s_st.deleted++;
            portEXIT_CRITICAL(&s_mux);
            if (!free_bytes(&free_b)) return true;
        }
    }
    if (free_b < s_min_free_bytes)
        ESP_LOGW(TAG, "only %llu MB free and no old logs left to delete",
                 (unsigned long long)(free_b / (1024 * 1024)));
    return free_b >= SD_LOG_HARD_MIN_FREE;
}

/* ---- file handling (writer task, or start() before the task exists) - */

static void sync_fp(FILE *fp)
{
    if (!fp) return;
    fflush(fp);
    int fd = fileno(fp);
    if (fd >= 0) fsync(fd);
}

/* Write the staging buffer to the current file. Frames counted as written
 * only once they reached FatFs. Without a file the data is dropped. */
static void stage_flush(void)
{
    if (s_stage_len == 0) return;
    size_t len = s_stage_len;
    uint32_t frames = s_stage_frames;
    s_stage_len = 0;
    s_stage_frames = 0;
    if (!s_fp) {
        portENTER_CRITICAL(&s_mux);
        s_st.dropped += frames;
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    size_t wr = fwrite(s_stage, 1, len, s_fp);
    s_file_bytes += (uint32_t)wr;
    portENTER_CRITICAL(&s_mux);
    s_st.bytes += wr;
    s_st.file_bytes = s_file_bytes;
    if (wr == len) {
        s_st.frames += frames;
    } else {
        s_st.write_errors++;
        s_st.dropped += frames;
    }
    portEXIT_CRITICAL(&s_mux);
    if (wr != len) {
        ESP_LOGE(TAG, "write failed on %s (%u of %u bytes, errno %d), closing",
                 s_file, (unsigned)wr, (unsigned)len, errno);
        fclose(s_fp);
        s_fp = NULL;
    }
}

static void close_files(void)
{
    stage_flush();
    if (s_fp) {
        sync_fp(s_fp);
        fclose(s_fp);
        s_fp = NULL;
    }
    if (s_mrk_fp) {
        sync_fp(s_mrk_fp);
        fclose(s_mrk_fp);
        s_mrk_fp = NULL;
    }
}

/* Open <prefix>_<seq>.csv (skipping names that already exist) and stage
 * the CSV header. */
static bool open_file(uint32_t seq)
{
    char name[SD_LOG_FILE_MAX];
    char path[80];
    struct stat st;
    for (int tries = 0; tries < 16; tries++, seq++) {
        snprintf(name, sizeof(name), "%s_%06" PRIu32 ".csv", s_prefix, seq);
        join_path(path, sizeof(path), name);
        if (stat(path, &st) != 0) break;
    }
    if (s_mrk_fp) {             /* markers follow the new data file */
        sync_fp(s_mrk_fp);
        fclose(s_mrk_fp);
        s_mrk_fp = NULL;
    }
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        ESP_LOGE(TAG, "cannot create %s (errno %d)", path, errno);
        return false;
    }
    setvbuf(fp, NULL, _IONBF, 0);
    s_fp = fp;
    s_seq = seq;
    s_file_bytes = 0;
    strlcpy(s_file, name, sizeof(s_file));
    memcpy(s_stage + s_stage_len, SD_LOG_CSV_HEADER, sizeof(SD_LOG_CSV_HEADER) - 1);
    s_stage_len += sizeof(SD_LOG_CSV_HEADER) - 1;
    portENTER_CRITICAL(&s_mux);
    strlcpy(s_st.file, name, sizeof(s_st.file));
    s_st.file_bytes = 0;
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "logging to %s/%s", SD_LOG_DIR, name);
    return true;
}

static bool rotate(void)
{
    close_files();
    portENTER_CRITICAL(&s_mux);
    s_st.rotations++;
    portEXIT_CRITICAL(&s_mux);
    if (!ensure_space(s_seq)) {
        ESP_LOGE(TAG, "card full, logging paused");
        return false;
    }
    return open_file(s_seq + 1);
}

/* ---- formatting ---------------------------------------------------- */

static const char k_hex[] = "0123456789ABCDEF";

static char *put_u64(char *p, uint64_t v)
{
    char tmp[20];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v && n < (int)sizeof(tmp));
    while (n) *p++ = tmp[--n];
    return p;
}

/* SavvyCAN native CSV row; returns its length (<= SD_LOG_LINE_MAX). */
static size_t format_frame(char *out, const log_rec_t *r)
{
    char *p = put_u64(out, (uint64_t)(r->ts_us < 0 ? 0 : r->ts_us));
    *p++ = ',';
    for (int s = 28; s >= 0; s -= 4) *p++ = k_hex[(r->id >> s) & 0xF];
    *p++ = ',';
    if (r->flags & SD_LOG_FLAG_EXT) { memcpy(p, "true,", 5); p += 5; }
    else                            { memcpy(p, "false,", 6); p += 6; }
    memcpy(p, (r->flags & SD_LOG_FLAG_TX) ? "Tx," : "Rx,", 3);
    p += 3;
    p = put_u64(p, r->bus);
    *p++ = ',';
    p = put_u64(p, r->dlc);
    *p++ = ',';
    for (int i = 0; i < 8; i++) {
        uint8_t b = (i < r->dlc) ? r->data[i] : 0;
        *p++ = k_hex[b >> 4];
        *p++ = k_hex[b & 0xF];
        *p++ = ',';
    }
    *p++ = '\n';
    return (size_t)(p - out);
}

static void write_mark(const log_rec_t *r)
{
    char label[SD_LOG_LABEL_MAX + 1];
    uint32_t slot = r->id;
    if (slot >= SD_LOG_MARK_SLOTS) return;
    portENTER_CRITICAL(&s_mux);
    memcpy(label, s_mark_label[slot], sizeof(label));
    s_mark_used[slot] = false;
    portEXIT_CRITICAL(&s_mux);

    if (!s_mrk_fp && s_file[0]) {
        char path[80];
        mrk_path(path, sizeof(path), s_file);
        struct stat st;
        bool fresh = stat(path, &st) != 0;
        s_mrk_fp = fopen(path, "ab");
        if (!s_mrk_fp) {
            ESP_LOGW(TAG, "cannot open %s (errno %d)", path, errno);
            return;
        }
        setvbuf(s_mrk_fp, NULL, _IONBF, 0);
        if (fresh) fputs(SD_LOG_MRK_HEADER, s_mrk_fp);
    }
    if (!s_mrk_fp) return;

    char line[SD_LOG_LABEL_MAX + 32];
    char *p = put_u64(line, (uint64_t)(r->ts_us < 0 ? 0 : r->ts_us));
    *p++ = ',';
    size_t n = strlen(label);
    memcpy(p, label, n);
    p += n;
    *p++ = '\n';
    size_t len = (size_t)(p - line);
    if (fwrite(line, 1, len, s_mrk_fp) == len) {
        portENTER_CRITICAL(&s_mux);
        s_st.marks++;
        portEXIT_CRITICAL(&s_mux);
    }
    ESP_LOGI(TAG, "mark @%" PRId64 " us: %s", r->ts_us, label);
}

/* ---- writer task --------------------------------------------------- */

/* Format up to SD_LOG_DRAIN_MAX records. Returns how many were consumed. */
static uint32_t drain(void)
{
    portENTER_CRITICAL(&s_mux);
    uint32_t avail = s_count;
    uint32_t tail = s_tail;
    portEXIT_CRITICAL(&s_mux);
    if (avail > SD_LOG_DRAIN_MAX) avail = SD_LOG_DRAIN_MAX;

    for (uint32_t i = 0; i < avail; i++) {
        const log_rec_t *r = &s_ring[tail];
        if (++tail == s_ring_size) tail = 0;
        if (r->kind == REC_MARK) {
            write_mark(r);
            continue;
        }
        if (s_fp && s_file_bytes + s_stage_len + SD_LOG_LINE_MAX > s_max_file_bytes) {
            stage_flush();
            (void)rotate();
        }
        if (s_stage_len + SD_LOG_LINE_MAX > SD_LOG_STAGE_SIZE) stage_flush();
        s_stage_len += format_frame(s_stage + s_stage_len, r);
        s_stage_frames++;
    }

    if (avail) {
        portENTER_CRITICAL(&s_mux);
        s_tail = tail;
        s_count -= avail;
        portEXIT_CRITICAL(&s_mux);
    }
    return avail;
}

static void writer_task(void *arg)
{
    (void)arg;
    int64_t last_sync = esp_timer_get_time();
    int64_t last_reopen = last_sync;
    for (;;) {
        bool stop = s_stop_req;
        uint32_t n = drain();
        int64_t now = esp_timer_get_time();

        if (!s_fp && !stop && (now - last_reopen) >= (int64_t)SD_LOG_REOPEN_MS * 1000) {
            /* Write error or full card earlier: try a fresh file. */
            last_reopen = now;
            if (ensure_space(s_seq)) (void)open_file(s_seq + 1);
        }
        if ((now - last_sync) >= (int64_t)SD_LOG_SYNC_MS * 1000) {
            last_sync = now;
            stage_flush();
            sync_fp(s_fp);
            sync_fp(s_mrk_fp);
        }

        if (stop) {
            portENTER_CRITICAL(&s_mux);
            bool empty = s_count == 0;
            portEXIT_CRITICAL(&s_mux);
            if (empty) break;
            continue;
        }
        /* Always give up at least one tick so IDLE on this core runs. */
        (void)ulTaskNotifyTake(pdTRUE, n ? 1 : pdMS_TO_TICKS(20));
    }
    close_files();
    xSemaphoreGive(s_done);
    /* sd_logger_stop() deletes us (a WithCaps task cannot free itself). */
    for (;;) vTaskSuspend(NULL);
}

static bool create_writer(void)
{
#if CONFIG_SPIRAM && CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
    if (xTaskCreatePinnedToCoreWithCaps(writer_task, "sd_log", SD_LOG_TASK_STACK, NULL,
                                        SD_LOG_TASK_PRIO, &s_task, SD_LOG_TASK_CORE,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
        s_task_ext_stack = true;
        return true;
    }
#endif
    s_task_ext_stack = false;
    return xTaskCreatePinnedToCore(writer_task, "sd_log", SD_LOG_TASK_STACK, NULL,
                                   SD_LOG_TASK_PRIO, &s_task, SD_LOG_TASK_CORE) == pdPASS;
}

static void delete_writer(void)
{
#if CONFIG_SPIRAM && CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
    if (s_task_ext_stack) {
        vTaskDeleteWithCaps(s_task);
        s_task = NULL;
        return;
    }
#endif
    vTaskDelete(s_task);
    s_task = NULL;
}

/* ---- public API ---------------------------------------------------- */

void sd_logger_push(int bus, uint32_t id, uint8_t flags,
                    const uint8_t *data, uint8_t dlc)
{
    if (!g_sd_logger_running) return;
    log_rec_t r;
    r.ts_us = esp_timer_get_time();
    r.id = id;
    r.bus = (uint8_t)bus;
    r.dlc = dlc > 8 ? 8 : dlc;
    r.flags = flags;
    r.kind = REC_FRAME;
    memset(r.data, 0, sizeof(r.data));
    if (data && r.dlc && !(flags & SD_LOG_FLAG_RTR)) memcpy(r.data, data, r.dlc);

    portENTER_CRITICAL(&s_mux);
    if (g_sd_logger_running && s_count < s_ring_size) {
        s_ring[s_head] = r;
        if (++s_head == s_ring_size) s_head = 0;
        if (++s_count > s_st.ring_high_water) s_st.ring_high_water = s_count;
    } else if (g_sd_logger_running) {
        s_st.dropped++;
    }
    portEXIT_CRITICAL(&s_mux);
}

bool sd_logger_mark(const char *label)
{
    if (!g_sd_logger_running || !label) return false;
    char clean[SD_LOG_LABEL_MAX + 1];
    size_t n = 0;
    while (*label == ' ') label++;
    for (; *label && n < SD_LOG_LABEL_MAX; label++) {
        char c = *label;
        if (c == ',') c = '_';
        else if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        clean[n++] = c;
    }
    while (n > 0 && clean[n - 1] == ' ') n--;
    clean[n] = '\0';
    if (n == 0) return false;

    log_rec_t r = {
        .ts_us = esp_timer_get_time(),
        .kind = REC_MARK,
    };
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    if (g_sd_logger_running && s_count < s_ring_size) {
        for (uint32_t i = 0; i < SD_LOG_MARK_SLOTS; i++) {
            if (s_mark_used[i]) continue;
            s_mark_used[i] = true;
            memcpy(s_mark_label[i], clean, n + 1);
            r.id = i;
            s_ring[s_head] = r;
            if (++s_head == s_ring_size) s_head = 0;
            s_count++;
            ok = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

static void free_buffers(void)
{
    heap_caps_free(s_stage);
    s_stage = NULL;
    heap_caps_free(s_ring);
    s_ring = NULL;
    s_ring_size = 0;
}

static bool alloc_buffers(void)
{
    s_ring_size = SD_LOG_RING_FRAMES;
    s_ring = heap_caps_malloc(sizeof(log_rec_t) * s_ring_size,
                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_ring) {
        s_ring_size = SD_LOG_RING_FALLBACK;
        s_ring = heap_caps_malloc(sizeof(log_rec_t) * s_ring_size,
                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    s_stage = heap_caps_malloc(SD_LOG_STAGE_SIZE,
                               MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_stage)
        s_stage = heap_caps_malloc(SD_LOG_STAGE_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_ring || !s_stage) {
        ESP_LOGE(TAG, "out of memory (ring %u x %u B, stage %u B)",
                 (unsigned)s_ring_size, (unsigned)sizeof(log_rec_t),
                 (unsigned)SD_LOG_STAGE_SIZE);
        free_buffers();
        return false;
    }
    return true;
}

bool sd_logger_start(const char *prefix)
{
    if (s_task) return true;
    if (!prefix || !*prefix) prefix = SD_LOG_PREFIX_DEFAULT;
    if (!prefix_valid(prefix)) {
        ESP_LOGW(TAG, "invalid prefix '%s' (1-%d chars of A-Z a-z 0-9 _ -)",
                 prefix, SD_LOG_PREFIX_MAX);
        return false;
    }
    if (!sd_is_mounted() && sd_mount() != ESP_OK) {
        ESP_LOGW(TAG, "no card mounted at %s", SD_MOUNT_POINT);
        return false;
    }
    if (mkdir(SD_LOG_DIR, 0775) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "cannot create %s (errno %d)", SD_LOG_DIR, errno);
        return false;
    }
    if (!s_done) s_done = xSemaphoreCreateBinaryStatic(&s_done_buf);
    if (!alloc_buffers()) return false;

    strlcpy(s_prefix, prefix, sizeof(s_prefix));
    s_stage_len = 0;
    s_stage_frames = 0;
    s_file[0] = '\0';
    portENTER_CRITICAL(&s_mux);
    memset(&s_st, 0, sizeof(s_st));
    memset(s_mark_used, 0, sizeof(s_mark_used));
    s_head = s_tail = s_count = 0;
    portEXIT_CRITICAL(&s_mux);

    uint32_t max_seq = 0;
    (void)scan_logs(&max_seq, NULL, 0, 0);
    s_seq = max_seq;
    if (!ensure_space(0) || !open_file(max_seq + 1)) {
        close_files();
        free_buffers();
        return false;
    }

    s_stop_req = false;
    xSemaphoreTake(s_done, 0);
    portENTER_CRITICAL(&s_mux);
    g_sd_logger_running = true;
    portEXIT_CRITICAL(&s_mux);
    if (!create_writer()) {
        portENTER_CRITICAL(&s_mux);
        g_sd_logger_running = false;
        portEXIT_CRITICAL(&s_mux);
        s_task = NULL;
        ESP_LOGE(TAG, "cannot create writer task");
        close_files();
        free_buffers();
        return false;
    }
    ESP_LOGI(TAG, "started: ring %u frames, %s stack, rotate at %u MB, keep %llu MB free",
             (unsigned)s_ring_size, s_task_ext_stack ? "PSRAM" : "internal", (unsigned)(s_max_file_bytes / (1024 * 1024)),
             (unsigned long long)(s_min_free_bytes / (1024 * 1024)));
    return true;
}

bool sd_logger_stop(void)
{
    if (!s_task) return true;
    portENTER_CRITICAL(&s_mux);
    g_sd_logger_running = false;   /* producers stop before the final drain */
    portEXIT_CRITICAL(&s_mux);
    s_stop_req = true;
    xTaskNotifyGive(s_task);
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(SD_LOG_STOP_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "writer did not stop within %d ms", SD_LOG_STOP_TIMEOUT_MS);
        return false;
    }
    delete_writer();
    free_buffers();
    ESP_LOGI(TAG, "stopped: %" PRIu32 " frames, %" PRIu32 " dropped, %llu bytes, %" PRIu32 " rotations",
             s_st.frames, s_st.dropped, (unsigned long long)s_st.bytes, s_st.rotations);
    return true;
}

void sd_logger_set_limits(uint32_t max_file_bytes, uint64_t min_free_bytes)
{
    if (max_file_bytes < SD_LOG_LINE_MAX * 16) max_file_bytes = SD_LOG_LINE_MAX * 16;
    s_max_file_bytes = max_file_bytes;
    s_min_free_bytes = min_free_bytes;
}

void sd_logger_get_status(sd_logger_status_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_mux);
    *out = s_st;
    out->ring_used = s_count;
    portEXIT_CRITICAL(&s_mux);
    out->running = s_task != NULL;
    out->ring_size = s_ring_size ? s_ring_size : SD_LOG_RING_FRAMES;
    strlcpy(out->prefix, s_prefix, sizeof(out->prefix));
    out->max_file_bytes = s_max_file_bytes;
    out->min_free_bytes = s_min_free_bytes;
    out->stack_free = s_task ? (uint32_t)uxTaskGetStackHighWaterMark(s_task) : 0;
}

/* ---- console ------------------------------------------------------- */

static void cmd_log_status(void)
{
    sd_logger_status_t st;
    sd_logger_get_status(&st);
    printf("sd log: %s%s%s\n", st.running ? "running" : "stopped",
           st.file[0] ? (st.running ? "  file " : "  last file ") : "",
           st.file);
    printf("  dir %s  prefix %s  rotate at %" PRIu32 " MB  keep %llu MB free  sync %d ms\n",
           SD_LOG_DIR, st.prefix, st.max_file_bytes / (1024 * 1024),
           (unsigned long long)(st.min_free_bytes / (1024 * 1024)), SD_LOG_SYNC_MS);
    printf("  frames %" PRIu32 "  dropped %" PRIu32 "  marks %" PRIu32 "  bytes %llu  file %" PRIu32 " B\n",
           st.frames, st.dropped, st.marks, (unsigned long long)st.bytes, st.file_bytes);
    printf("  rotations %" PRIu32 "  deleted %" PRIu32 "  write errors %" PRIu32 "\n",
           st.rotations, st.deleted, st.write_errors);
    printf("  ring %" PRIu32 "/%" PRIu32 "  high-water %" PRIu32 "%s",
           st.ring_used, st.ring_size, st.ring_high_water,
           st.running ? "" : "\n");
    if (st.running) printf("  writer stack free %" PRIu32 " B\n", st.stack_free);
    uint64_t free_b = 0;
    if (sd_is_mounted() && free_bytes(&free_b))
        printf("  card free %llu MB\n", (unsigned long long)(free_b / (1024 * 1024)));
    else
        printf("  card not mounted\n");
}

static void cmd_log_ls(void)
{
    if (!sd_is_mounted()) {
        printf("sd: not mounted (try 'sd mount')\n");
        return;
    }
    DIR *dir = opendir(SD_LOG_DIR);
    if (!dir) {
        printf("sd log: %s does not exist yet\n", SD_LOG_DIR);
        return;
    }
    char cur[SD_LOG_FILE_MAX];
    portENTER_CRITICAL(&s_mux);
    strlcpy(cur, s_task ? s_st.file : "", sizeof(cur));
    portEXIT_CRITICAL(&s_mux);
    int n = 0;
    uint64_t total = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char path[300];
        snprintf(path, sizeof(path), "%s/%s", SD_LOG_DIR, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0) {
            printf("  %-32s  ?\n", ent->d_name);
        } else {
            printf("%c %-32s  %llu\n", strcmp(ent->d_name, cur) == 0 ? '*' : ' ',
                   ent->d_name, (unsigned long long)st.st_size);
            total += (uint64_t)st.st_size;
        }
        n++;
    }
    closedir(dir);
    printf("%d file%s, %llu bytes in %s%s\n", n, n == 1 ? "" : "s",
           (unsigned long long)total, SD_LOG_DIR, cur[0] ? "  (* = open)" : "");
}

/* Argument after a fixed command word, taken from the original-case line. */
static const char *arg_after(const char *line, size_t cmd_len)
{
    if (strlen(line) < cmd_len) return "";
    const char *p = line + cmd_len;
    while (*p == ' ') p++;
    return p;
}

bool sd_logger_console(const char *line, const char *lower)
{
    if (strcmp(lower, "sd log") == 0 || strcmp(lower, "sd log help") == 0) {
        printf("sd log commands: start [prefix] | stop | status | mark <label> | ls\n"
               "                 limits <file_MB> [min_free_MB]\n");
        return true;
    }
    if (strcmp(lower, "sd log start") == 0 || strncmp(lower, "sd log start ", 13) == 0) {
        const char *prefix = arg_after(line, 12);
        if (s_task) {
            printf("sd log: already running (%s)\n", s_st.file);
        } else if (sd_logger_start(prefix)) {
            printf("sd log: started, writing %s/%s\n", SD_LOG_DIR, s_st.file);
        } else {
            printf("sd log: start failed (card mounted? prefix 1-%d chars of A-Z a-z 0-9 _ -)\n",
                   SD_LOG_PREFIX_MAX);
        }
        return true;
    }
    if (strcmp(lower, "sd log stop") == 0) {
        bool was = s_task != NULL;
        bool ok = sd_logger_stop();
        if (!was) printf("sd log: not running\n");
        else printf("sd log: %s\n", ok ? "stopped" : "stop timed out");
        if (was && ok) cmd_log_status();
        return true;
    }
    if (strcmp(lower, "sd log status") == 0) {
        cmd_log_status();
        return true;
    }
    if (strcmp(lower, "sd log ls") == 0) {
        cmd_log_ls();
        return true;
    }
    if (strcmp(lower, "sd log mark") == 0 || strncmp(lower, "sd log mark ", 12) == 0) {
        const char *label = arg_after(line, 11);
        if (!*label) printf("usage: sd log mark <label>\n");
        else printf("sd log mark: %s\n", sd_logger_mark(label) ? "ok" : "failed (not running?)");
        return true;
    }
    if (strncmp(lower, "sd log limits", 13) == 0) {
        unsigned file_mb = 0, free_mb = 0;
        int n = sscanf(lower + 13, "%u %u", &file_mb, &free_mb);
        if (n < 1 || file_mb == 0 || file_mb > 4095) {
            printf("usage: sd log limits <file_MB 1-4095> [min_free_MB]\n");
            return true;
        }
        sd_logger_set_limits((uint32_t)file_mb * 1024u * 1024u,
                             n >= 2 ? (uint64_t)free_mb * 1024u * 1024u : s_min_free_bytes);
        printf("sd log: rotate at %u MB, keep %llu MB free (until reboot)\n",
               (unsigned)(s_max_file_bytes / (1024 * 1024)),
               (unsigned long long)(s_min_free_bytes / (1024 * 1024)));
        return true;
    }
    return false;
}

#endif /* BOARD_HAS_SD */
