#include "storage/sd.h"

#include "board/board.h"

#if defined(BOARD_HAS_SD) && BOARD_HAS_SD

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "storage/sd_logger.h"

static const char *TAG = "sd";

/* Conservative ladder for jumper-wired / soldered breakouts.
 * Card init itself probes at 400 kHz; these are the post-init data rates. */
static const int k_freq_khz[] = { 20000, 10000, 4000, 1000 };
#define SD_FREQ_COUNT         (sizeof(k_freq_khz) / sizeof(k_freq_khz[0]))
#define SD_MAX_FILES          4
#define SD_TEST_PATH          SD_MOUNT_POINT "/_dorky_test.bin"
#define SD_TEST_SIZE          (256 * 1024)
#define SD_TEST_CHUNK         (4 * 1024)   /* internal DMA RAM, keep it small */
#define SD_RETRY_DELAY_MS     80

static sdmmc_card_t *s_card;
static bool s_bus_up;
static bool s_mounted;
static int s_freq_khz;

/* Last successful sdmmc_card_init, even if the FAT mount later failed.
 * Lets `sd status` show CID/CSD after a raw-OK / FAT-fail attempt. */
static bool s_probed;
static sdmmc_card_t s_probe;
static int s_probe_freq_khz;
static esp_err_t s_last_fat_err;

static const char *card_type_str(const sdmmc_card_t *card)
{
    if (!card) return "?";
    if (card->is_mmc) return "MMC";
    if (card->is_sdio) return "SDIO";
    /* IDF normalises csd.capacity to 512-byte sectors. CCS => SDHC/SDXC. */
    if ((card->ocr & (1u << 30)) == 0) return "SDSC";
    uint64_t bytes = (uint64_t)card->csd.capacity * card->csd.sector_size;
    return (bytes > (32ull * 1024 * 1024 * 1024)) ? "SDXC" : "SDHC";
}

/* CID product name is a raw 8-byte field, not necessarily NUL-terminated. */
static void card_name(const sdmmc_card_t *card, char out[9])
{
    memset(out, 0, 9);
    if (card) memcpy(out, card->cid.name, sizeof(card->cid.name));
    for (int i = 7; i >= 0 && (out[i] == '\0' || out[i] == ' '); i--)
        out[i] = '\0';
    if (!out[0]) {
        out[0] = '?';
        out[1] = '\0';
    }
}

static uint64_t card_bytes(const sdmmc_card_t *card)
{
    if (!card) return 0;
    return (uint64_t)card->csd.capacity * card->csd.sector_size;
}

static void print_card_ident(const sdmmc_card_t *card, const char *prefix)
{
    char name[9];
    card_name(card, name);
    int real = (card->real_freq_khz > 0) ? card->real_freq_khz : 0;
    printf("%s card '%s' %s  ocr=0x%08lx  sectors=%d x %d  size=%llu MB  real=%d kHz\n",
           prefix, name, card_type_str(card),
           (unsigned long)card->ocr,
           card->csd.capacity, card->csd.sector_size,
           (unsigned long long)(card_bytes(card) / (1024 * 1024)),
           real);
}

static void remember_probe(const sdmmc_card_t *card, int freq_khz)
{
    s_probe = *card;
    s_probe_freq_khz = freq_khz;
    s_probed = true;
}

static void apply_sd_pullups(void)
{
    const gpio_num_t pins[] = {
        BOARD_SD_SCLK_GPIO, BOARD_SD_MISO_GPIO,
        BOARD_SD_MOSI_GPIO, BOARD_SD_CS_GPIO,
    };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_pullup_en(pins[i]);
        gpio_pulldown_dis(pins[i]);
    }
}

static esp_err_t ensure_bus(void)
{
    if (s_bus_up) {
        apply_sd_pullups();
        return ESP_OK;
    }

    /* CS high before the first clock so the card stays deselected. */
    gpio_reset_pin(BOARD_SD_CS_GPIO);
    gpio_set_direction(BOARD_SD_CS_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BOARD_SD_CS_GPIO, 1);
    apply_sd_pullups();

    spi_bus_config_t bus = {
        .mosi_io_num = BOARD_SD_MOSI_GPIO,
        .miso_io_num = BOARD_SD_MISO_GPIO,
        .sclk_io_num = BOARD_SD_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
    };
    /* SPI3 only. The MCP2518FD already owns SPI2 (SPI_DMA_DISABLED). */
    esp_err_t err = spi_bus_initialize(BOARD_SD_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_bus_up = true;
        apply_sd_pullups();
        return ESP_OK;
    }
    ESP_LOGE(TAG, "SPI3 bus init failed: %s", esp_err_to_name(err));
    return err;
}

static void settle_between_retries(void)
{
    gpio_set_level(BOARD_SD_CS_GPIO, 1);
    apply_sd_pullups();
    vTaskDelay(pdMS_TO_TICKS(SD_RETRY_DELAY_MS));
}

/* Re-run sdmmc_card_init after a failed VFS mount so we can print CID/CSD
 * when the raw SPI link works but the FAT mount (sector 0) does not.
 * The failed esp_vfs_fat_sdspi_mount already removed the SDSPI device. */
static esp_err_t probe_card_at(int freq_khz, bool verbose)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BOARD_SD_SPI_HOST;
    host.max_freq_khz = freq_khz;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = BOARD_SD_SPI_HOST;
    slot.gpio_cs = BOARD_SD_CS_GPIO;
    slot.gpio_cd = SDSPI_SLOT_NO_CD;
    slot.gpio_wp = SDSPI_SLOT_NO_WP;

    apply_sd_pullups();

    esp_err_t err = sdspi_host_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        if (verbose)
            printf("sd: sdspi_host_init failed: %s (%d)\n", esp_err_to_name(err), (int)err);
        return err;
    }

    sdspi_dev_handle_t handle = -1;
    err = sdspi_host_init_device(&slot, &handle);
    if (err != ESP_OK) {
        if (verbose)
            printf("sd: sdspi_host_init_device failed: %s (%d)\n",
                   esp_err_to_name(err), (int)err);
        return err;
    }
    host.slot = handle;

    sdmmc_card_t card;
    memset(&card, 0, sizeof(card));
    err = sdmmc_card_init(&host, &card);
    if (err == ESP_OK) {
        remember_probe(&card, freq_khz);
        if (verbose) {
            printf("sd: raw init OK at %d kHz (FAT mount still failed)\n", freq_khz);
            print_card_ident(&card, "  ");
        } else {
            char name[9];
            card_name(&card, name);
            ESP_LOGI(TAG, "raw init OK at %d kHz, card '%s' %s, FAT mount failed",
                     freq_khz, name, card_type_str(&card));
        }
    } else if (verbose) {
        printf("sd: raw init also failed at %d kHz: %s (%d)\n",
               freq_khz, esp_err_to_name(err), (int)err);
    }

    (void)sdspi_host_remove_device(handle);
    return err;
}

static void fill_mount_cfg(esp_vfs_fat_mount_config_t *cfg, bool format_if_failed)
{
    *cfg = (esp_vfs_fat_mount_config_t){
        .format_if_mount_failed = format_if_failed,
        .max_files = SD_MAX_FILES,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };
}

static esp_err_t mount_at(int freq_khz, bool format_if_failed)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BOARD_SD_SPI_HOST;
    host.max_freq_khz = freq_khz;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = BOARD_SD_SPI_HOST;
    slot.gpio_cs = BOARD_SD_CS_GPIO;
    slot.gpio_cd = SDSPI_SLOT_NO_CD;
    slot.gpio_wp = SDSPI_SLOT_NO_WP;

    apply_sd_pullups();

    esp_vfs_fat_mount_config_t mount_cfg;
    fill_mount_cfg(&mount_cfg, format_if_failed);

    sdmmc_card_t *card = NULL;
    esp_err_t err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot,
                                            &mount_cfg, &card);
    if (err != ESP_OK) return err;
    s_card = card;
    s_mounted = true;
    s_freq_khz = freq_khz;
    s_last_fat_err = ESP_OK;
    remember_probe(card, freq_khz);
    return ESP_OK;
}

static esp_err_t mount_ladder(bool format_if_failed, bool verbose)
{
    esp_err_t last = ESP_FAIL;
    for (size_t i = 0; i < SD_FREQ_COUNT; i++) {
        int freq = k_freq_khz[i];
        last = mount_at(freq, format_if_failed);
        if (last == ESP_OK) {
            char name[9];
            card_name(s_card, name);
            if (verbose)
                printf("sd: mounted %s at %d kHz%s\n", SD_MOUNT_POINT, freq,
                       format_if_failed ? " (formatted)" : "");
            else
                ESP_LOGI(TAG, "mounted %s at %d kHz, card '%s' %s",
                         SD_MOUNT_POINT, freq, name, card_type_str(s_card));
            if (verbose) print_card_ident(s_card, "  ");
            return ESP_OK;
        }

        s_last_fat_err = last;
        if (verbose)
            printf("sd: %d kHz failed (%s, %d)%s\n",
                   freq, esp_err_to_name(last), (int)last,
                   (i + 1 < SD_FREQ_COUNT) ? ", trying slower" : "");
        else
            ESP_LOGI(TAG, "%d kHz failed (%s)%s",
                     freq, esp_err_to_name(last),
                     (i + 1 < SD_FREQ_COUNT) ? ", trying slower" : "");

        /* Distinguish "card talks, FAT/sector 0 failed" from "no card". */
        (void)probe_card_at(freq, verbose);
        settle_between_retries();
    }
    return last;
}

static esp_err_t do_mount(bool verbose)
{
    if (s_mounted) {
        if (verbose) printf("sd: already mounted at %s (%d kHz)\n",
                            SD_MOUNT_POINT, s_freq_khz);
        return ESP_OK;
    }

    esp_err_t err = ensure_bus();
    if (err != ESP_OK) {
        if (verbose)
            printf("sd: SPI3 init failed: %s (%d)\n", esp_err_to_name(err), (int)err);
        return err;
    }

    err = mount_ladder(false, verbose);
    if (err == ESP_OK) return ESP_OK;

    if (verbose) {
        printf("sd: mount failed at 20/10/4/1 MHz, last error %s (%d)\n",
               esp_err_to_name(err), (int)err);
        if (s_probed)
            print_card_ident(&s_probe, "  last raw init:");
        else
            printf("  no card responded to raw init either\n");
        printf("  If the card is exFAT or unformatted: 'sd format yes' writes MBR + FAT32.\n");
    } else {
        ESP_LOGI(TAG, "no card or mount failed (last %s) — use 'sd mount' when inserted",
                 esp_err_to_name(err));
    }
    return err;
}

static esp_err_t do_unmount(bool verbose)
{
    if (!s_mounted) {
        if (verbose) printf("sd: already unmounted\n");
        return ESP_OK;
    }
    if (sd_logger_is_running()) {
        if (verbose) printf("sd: stopping CAN log first\n");
        (void)sd_logger_stop();
    }
    esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    s_card = NULL;
    s_mounted = false;
    s_freq_khz = 0;
    if (err != ESP_OK) {
        if (verbose)
            printf("sd: unmount failed: %s (%d)\n", esp_err_to_name(err), (int)err);
        else
            ESP_LOGW(TAG, "unmount failed: %s", esp_err_to_name(err));
        return err;
    }
    if (verbose) printf("sd: unmounted %s\n", SD_MOUNT_POINT);
    else ESP_LOGI(TAG, "unmounted %s", SD_MOUNT_POINT);
    settle_between_retries();
    return ESP_OK;
}

static int real_freq_khz(void)
{
    const sdmmc_card_t *c = s_mounted ? s_card : (s_probed ? &s_probe : NULL);
    if (c && c->real_freq_khz > 0) return c->real_freq_khz;
    return s_mounted ? s_freq_khz : s_probe_freq_khz;
}

static void print_fat_size(void)
{
    uint64_t total = 0, free_b = 0;
    esp_err_t err = esp_vfs_fat_info(SD_MOUNT_POINT, &total, &free_b);
    if (err == ESP_OK) {
        printf("  size: %llu MB  free: %llu MB\n",
               (unsigned long long)(total / (1024 * 1024)),
               (unsigned long long)(free_b / (1024 * 1024)));
    } else if (s_card) {
        printf("  size: %llu MB  free: ? (%s)\n",
               (unsigned long long)(card_bytes(s_card) / (1024 * 1024)),
               esp_err_to_name(err));
    }
}

static void cmd_status(void)
{
    printf("sd: %s\n", s_mounted ? "mounted" : "unmounted");
    printf("  pins: SPI3  sclk=%d miso=%d mosi=%d cs=%d  mount=%s\n",
           (int)BOARD_SD_SCLK_GPIO, (int)BOARD_SD_MISO_GPIO,
           (int)BOARD_SD_MOSI_GPIO, (int)BOARD_SD_CS_GPIO, SD_MOUNT_POINT);
    if (s_mounted && s_card) {
        print_card_ident(s_card, " ");
        printf("  speed: %d kHz\n", real_freq_khz());
        print_fat_size();
        return;
    }
    if (s_probed) {
        printf("  last raw init at %d kHz (FAT not mounted", s_probe_freq_khz);
        if (s_last_fat_err != ESP_OK)
            printf(", %s / %d", esp_err_to_name(s_last_fat_err), (int)s_last_fat_err);
        printf("):\n");
        print_card_ident(&s_probe, " ");
    } else if (s_last_fat_err != ESP_OK) {
        printf("  last mount error: %s (%d)\n",
               esp_err_to_name(s_last_fat_err), (int)s_last_fat_err);
    }
}

static void cmd_ls(void)
{
    if (!s_mounted) {
        printf("sd: not mounted (try 'sd mount')\n");
        return;
    }
    DIR *dir = opendir(SD_MOUNT_POINT);
    if (!dir) {
        printf("sd: cannot open %s\n", SD_MOUNT_POINT);
        return;
    }
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        char path[288];
        snprintf(path, sizeof(path), "%s/%s", SD_MOUNT_POINT, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0) {
            printf("  %-32s  ?\n", ent->d_name);
        } else if (S_ISDIR(st.st_mode)) {
            printf("  %-32s  <dir>\n", ent->d_name);
        } else {
            printf("  %-32s  %llu\n", ent->d_name, (unsigned long long)st.st_size);
        }
        n++;
    }
    closedir(dir);
    printf("%d entr%s in %s\n", n, n == 1 ? "y" : "ies", SD_MOUNT_POINT);
}

static void fill_chunk(uint8_t *buf, size_t n, uint32_t offset)
{
    for (size_t i = 0; i < n; i++)
        buf[i] = (uint8_t)(((offset + (uint32_t)i) * 131u) + 17u);
}

static void cmd_test(void)
{
    if (!s_mounted) {
        printf("sd: not mounted, trying mount first\n");
        if (do_mount(true) != ESP_OK) return;
    }

    /* Internal DMA-capable RAM — do not hand a PSRAM buffer to SDSPI. */
    uint8_t *buf = (uint8_t *)heap_caps_malloc(SD_TEST_CHUNK,
                                               MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf)
        buf = (uint8_t *)heap_caps_malloc(SD_TEST_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) {
        printf("sd test: no internal DMA memory for %d-byte buffer\n", SD_TEST_CHUNK);
        return;
    }

    printf("sd test: write+verify+delete %d KB in %d KB chunks at %s\n",
           SD_TEST_SIZE / 1024, SD_TEST_CHUNK / 1024, SD_TEST_PATH);

    bool ok = false;
    int64_t t_write = 0, t_read = 0;
    FILE *f = fopen(SD_TEST_PATH, "wb");
    if (!f) {
        printf("sd test: fopen(write) failed\n");
        goto out;
    }
    int64_t t0 = esp_timer_get_time();
    for (uint32_t off = 0; off < SD_TEST_SIZE; off += SD_TEST_CHUNK) {
        fill_chunk(buf, SD_TEST_CHUNK, off);
        if (fwrite(buf, 1, SD_TEST_CHUNK, f) != SD_TEST_CHUNK) {
            printf("sd test: write failed at offset %u\n", (unsigned)off);
            fclose(f);
            goto out_rm;
        }
    }
    if (fflush(f) != 0) {
        printf("sd test: fflush failed\n");
        fclose(f);
        goto out_rm;
    }
    int fd = fileno(f);
    if (fd >= 0) fsync(fd);
    fclose(f);
    t_write = esp_timer_get_time() - t0;

    f = fopen(SD_TEST_PATH, "rb");
    if (!f) {
        printf("sd test: fopen(read) failed\n");
        goto out_rm;
    }
    t0 = esp_timer_get_time();
    uint8_t *expect = (uint8_t *)heap_caps_malloc(SD_TEST_CHUNK,
                                                  MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!expect)
        expect = (uint8_t *)heap_caps_malloc(SD_TEST_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!expect) {
        printf("sd test: no memory for verify buffer\n");
        fclose(f);
        goto out_rm;
    }
    for (uint32_t off = 0; off < SD_TEST_SIZE; off += SD_TEST_CHUNK) {
        fill_chunk(expect, SD_TEST_CHUNK, off);
        if (fread(buf, 1, SD_TEST_CHUNK, f) != SD_TEST_CHUNK) {
            printf("sd test: read failed at offset %u\n", (unsigned)off);
            fclose(f);
            heap_caps_free(expect);
            goto out_rm;
        }
        if (memcmp(buf, expect, SD_TEST_CHUNK) != 0) {
            printf("sd test: verify mismatch at offset %u\n", (unsigned)off);
            fclose(f);
            heap_caps_free(expect);
            goto out_rm;
        }
    }
    fclose(f);
    t_read = esp_timer_get_time() - t0;
    heap_caps_free(expect);
    ok = true;

out_rm:
    if (unlink(SD_TEST_PATH) != 0)
        printf("sd test: could not remove %s\n", SD_TEST_PATH);
    else if (ok)
        printf("  removed %s\n", SD_TEST_PATH);

    if (ok) {
        double w_s = t_write / 1e6;
        double r_s = t_read / 1e6;
        double w_kbs = w_s > 0 ? (SD_TEST_SIZE / 1024.0) / w_s : 0;
        double r_kbs = r_s > 0 ? (SD_TEST_SIZE / 1024.0) / r_s : 0;
        printf("  write: %.0f KB/s (%.0f ms)\n", w_kbs, t_write / 1000.0);
        printf("  read : %.0f KB/s (%.0f ms)  verify OK\n", r_kbs, t_read / 1000.0);
    }

out:
    heap_caps_free(buf);
}

static void cmd_format(const char *lower)
{
    /* `lower` is the whole lower-cased line. */
    if (strcmp(lower, "sd format yes") != 0) {
        printf("sd format will ERASE the card, write a new MBR partition and a FAT32 filesystem.\n"
               "All existing files will be lost. Type 'sd format yes' to confirm.\n");
        return;
    }

    esp_err_t err = ensure_bus();
    if (err != ESP_OK) {
        printf("sd format: SPI3 init failed: %s (%d)\n", esp_err_to_name(err), (int)err);
        return;
    }

    if (s_mounted) {
        printf("sd format: unmounting first\n");
        if (do_unmount(true) != ESP_OK) return;
    }

    printf("sd format: trying to mount the existing filesystem...\n");
    err = mount_ladder(false, true);
    if (err != ESP_OK) {
        printf("sd format: no usable FAT filesystem, partitioning + FAT32 format at 20/10/4/1 MHz...\n");
        settle_between_retries();
        err = mount_ladder(true, true);
        if (err != ESP_OK) {
            printf("sd format: failed (%s, %d). Check wiring / try a slower card, then 'sd mount'.\n",
                   esp_err_to_name(err), (int)err);
            if (s_probed) print_card_ident(&s_probe, "  last raw init:");
            return;
        }
        /* format_if_mount_failed already ran f_fdisk + f_mkfs. */
        printf("sd format: MBR + FAT32 written while mounting.\n");
    } else {
        printf("sd format: existing FAT mounted, rewriting FAT32...\n");
    }

    if (s_mounted && s_card) {
        err = esp_vfs_fat_sdcard_format(SD_MOUNT_POINT, s_card);
        if (err != ESP_OK) {
            printf("sd format: esp_vfs_fat_sdcard_format failed: %s (%d)\n",
                   esp_err_to_name(err), (int)err);
            /* The format API unmounts and recycles on remount failure. */
            s_card = NULL;
            s_mounted = false;
            s_freq_khz = 0;
            printf("sd format: trying a remount...\n");
            settle_between_retries();
            if (mount_ladder(false, true) != ESP_OK) return;
        } else {
            printf("sd format: FAT32 rewritten.\n");
        }
    }

    if (!s_mounted) {
        printf("sd format: card not mounted afterwards\n");
        return;
    }
    printf("sd format: done, remounted %s at %d kHz\n", SD_MOUNT_POINT, s_freq_khz);
    print_card_ident(s_card, " ");
    print_fat_size();
}

bool sd_is_mounted(void)
{
    return s_mounted;
}

esp_err_t sd_mount(void)
{
    return do_mount(false);
}

void sd_init(void)
{
    ESP_LOGI(TAG, "SPI3 microSD pins sclk=%d miso=%d mosi=%d cs=%d",
             (int)BOARD_SD_SCLK_GPIO, (int)BOARD_SD_MISO_GPIO,
             (int)BOARD_SD_MOSI_GPIO, (int)BOARD_SD_CS_GPIO);
    (void)do_mount(false);
}

bool sd_console(const char *line, const char *lower)
{
    if (strcmp(lower, "help") == 0) {
        printf("          sd status | sd mount | sd unmount | sd ls | sd test | sd format yes\n"
               "          sd log start [prefix] | sd log stop | sd log status | sd log mark <label> | sd log ls\n");
        return true;
    }
    if (strcmp(lower, "sd") == 0 || strcmp(lower, "sd help") == 0) {
        printf("sd commands: status | mount | unmount | ls | test | format yes | log ...\n");
        return true;
    }
    if (strncmp(lower, "sd log", 6) == 0 && (lower[6] == '\0' || lower[6] == ' ')) {
        if (!sd_logger_console(line, lower))
            printf("unknown: %s  (try: sd log help)\n", line);
        return true;
    }
    if (strcmp(lower, "sd status") == 0) {
        cmd_status();
        return true;
    }
    if (strcmp(lower, "sd mount") == 0) {
        (void)do_mount(true);
        return true;
    }
    if (strcmp(lower, "sd unmount") == 0) {
        (void)do_unmount(true);
        return true;
    }
    if (strcmp(lower, "sd ls") == 0) {
        cmd_ls();
        return true;
    }
    if (strcmp(lower, "sd test") == 0) {
        cmd_test();
        return true;
    }
    if (strncmp(lower, "sd format", 9) == 0) {
        cmd_format(lower);
        return true;
    }
    return false;
}

#else  /* !BOARD_HAS_SD */

bool sd_is_mounted(void)
{
    return false;
}

esp_err_t sd_mount(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

void sd_init(void) {}

bool sd_console(const char *line, const char *lower)
{
    (void)line;
    (void)lower;
    return false;
}

#endif
