#include "storage/sd.h"

#include "board/board.h"

#if defined(BOARD_HAS_SD) && BOARD_HAS_SD

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd";

#define SD_FREQ_FAST_KHZ  20000
#define SD_FREQ_SLOW_KHZ   4000
#define SD_MAX_FILES          4
#define SD_TEST_PATH      SD_MOUNT_POINT "/_dorky_test.bin"
#define SD_TEST_SIZE      (256 * 1024)
#define SD_TEST_CHUNK     (16 * 1024)

static sdmmc_card_t *s_card;
static bool s_bus_up;
static bool s_mounted;
static int s_freq_khz;

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

static esp_err_t ensure_bus(void)
{
    if (s_bus_up) return ESP_OK;

    spi_bus_config_t bus = {
        .mosi_io_num = BOARD_SD_MOSI_GPIO,
        .miso_io_num = BOARD_SD_MISO_GPIO,
        .sclk_io_num = BOARD_SD_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32 * 1024,
    };
    /* SPI3 only. The MCP2518FD already owns SPI2 (SPI_DMA_DISABLED). */
    esp_err_t err = spi_bus_initialize(BOARD_SD_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_bus_up = true;
        return ESP_OK;
    }
    ESP_LOGE(TAG, "SPI3 bus init failed: %s", esp_err_to_name(err));
    return err;
}

static esp_err_t mount_at(int freq_khz)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BOARD_SD_SPI_HOST;
    host.max_freq_khz = freq_khz;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = BOARD_SD_SPI_HOST;
    slot.gpio_cs = BOARD_SD_CS_GPIO;
    slot.gpio_cd = SDSPI_SLOT_NO_CD;
    slot.gpio_wp = SDSPI_SLOT_NO_WP;

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = SD_MAX_FILES,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
    };

    sdmmc_card_t *card = NULL;
    esp_err_t err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot,
                                            &mount_cfg, &card);
    if (err != ESP_OK) return err;
    s_card = card;
    s_mounted = true;
    s_freq_khz = freq_khz;
    return ESP_OK;
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

    err = mount_at(SD_FREQ_FAST_KHZ);
    if (err == ESP_OK) {
        char name[9];
        card_name(s_card, name);
        if (verbose)
            printf("sd: mounted %s at %d kHz\n", SD_MOUNT_POINT, SD_FREQ_FAST_KHZ);
        else
            ESP_LOGI(TAG, "mounted %s at %d kHz, card '%s' %s",
                     SD_MOUNT_POINT, SD_FREQ_FAST_KHZ, name, card_type_str(s_card));
        return ESP_OK;
    }

    if (verbose)
        printf("sd: 20 MHz failed (%s, %d), retrying at 4 MHz\n",
               esp_err_to_name(err), (int)err);
    else
        ESP_LOGI(TAG, "20 MHz failed (%s), retrying at 4 MHz", esp_err_to_name(err));

    esp_err_t err_slow = mount_at(SD_FREQ_SLOW_KHZ);
    if (err_slow == ESP_OK) {
        char name[9];
        card_name(s_card, name);
        if (verbose)
            printf("sd: mounted %s at %d kHz\n", SD_MOUNT_POINT, SD_FREQ_SLOW_KHZ);
        else
            ESP_LOGI(TAG, "mounted %s at %d kHz, card '%s' %s",
                     SD_MOUNT_POINT, SD_FREQ_SLOW_KHZ, name, card_type_str(s_card));
        return ESP_OK;
    }

    if (verbose)
        printf("sd: mount failed at 20 MHz (%s, %d) and 4 MHz (%s, %d)\n",
               esp_err_to_name(err), (int)err,
               esp_err_to_name(err_slow), (int)err_slow);
    else
        ESP_LOGI(TAG, "no card or mount failed (20 MHz: %s, 4 MHz: %s) — use 'sd mount' when inserted",
                 esp_err_to_name(err), esp_err_to_name(err_slow));
    return err_slow;
}

static esp_err_t do_unmount(bool verbose)
{
    if (!s_mounted) {
        if (verbose) printf("sd: already unmounted\n");
        return ESP_OK;
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
    return ESP_OK;
}

static int real_freq_khz(void)
{
    if (s_card && s_card->real_freq_khz > 0) return s_card->real_freq_khz;
    return s_freq_khz;
}

static void cmd_status(void)
{
    printf("sd: %s\n", s_mounted ? "mounted" : "unmounted");
    printf("  pins: SPI3  sclk=%d miso=%d mosi=%d cs=%d  mount=%s\n",
           (int)BOARD_SD_SCLK_GPIO, (int)BOARD_SD_MISO_GPIO,
           (int)BOARD_SD_MOSI_GPIO, (int)BOARD_SD_CS_GPIO, SD_MOUNT_POINT);
    if (!s_mounted || !s_card) return;

    char name[9];
    card_name(s_card, name);
    printf("  card: %s  type: %s  speed: %d kHz\n",
           name, card_type_str(s_card), real_freq_khz());

    uint64_t total = 0, free_b = 0;
    esp_err_t err = esp_vfs_fat_info(SD_MOUNT_POINT, &total, &free_b);
    if (err == ESP_OK) {
        printf("  size: %llu MB  free: %llu MB\n",
               (unsigned long long)(total / (1024 * 1024)),
               (unsigned long long)(free_b / (1024 * 1024)));
    } else {
        uint64_t bytes = (uint64_t)s_card->csd.capacity * s_card->csd.sector_size;
        printf("  size: %llu MB  free: ? (%s)\n",
               (unsigned long long)(bytes / (1024 * 1024)),
               esp_err_to_name(err));
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

    uint8_t *buf = (uint8_t *)heap_caps_malloc(SD_TEST_CHUNK,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf)
        buf = (uint8_t *)heap_caps_malloc(SD_TEST_CHUNK, MALLOC_CAP_8BIT);
    if (!buf) {
        printf("sd test: no memory for %d-byte buffer\n", SD_TEST_CHUNK);
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
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!expect)
        expect = (uint8_t *)heap_caps_malloc(SD_TEST_CHUNK, MALLOC_CAP_8BIT);
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

void sd_init(void)
{
    ESP_LOGI(TAG, "SPI3 microSD pins sclk=%d miso=%d mosi=%d cs=%d",
             (int)BOARD_SD_SCLK_GPIO, (int)BOARD_SD_MISO_GPIO,
             (int)BOARD_SD_MOSI_GPIO, (int)BOARD_SD_CS_GPIO);
    (void)do_mount(false);
}

bool sd_console(const char *line, const char *lower)
{
    (void)line;
    if (strcmp(lower, "help") == 0) {
        printf("          sd status | sd mount | sd unmount | sd ls | sd test\n");
        return true;
    }
    if (strcmp(lower, "sd") == 0 || strcmp(lower, "sd help") == 0) {
        printf("sd commands: status | mount | unmount | ls | test\n");
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
    return false;
}

#else  /* !BOARD_HAS_SD */

void sd_init(void) {}

bool sd_console(const char *line, const char *lower)
{
    (void)line;
    (void)lower;
    return false;
}

#endif
