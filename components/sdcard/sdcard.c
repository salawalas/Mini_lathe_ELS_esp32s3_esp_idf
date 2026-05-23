// sdcard.c – karta SD SPI + FATFS (współdzielona magistrala SPI2 z TFT)
// MISO skonfigurowany przez display_init() jako XPT_MISO=13 (IO13)
#include "sdcard.h"
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"

static const char *TAG = "SDCARD";

#define SD_MOUNT_POINT "/sdcard"
#define SD_MAX_FILES 64

static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;

esp_err_t sdcard_init(void)
{
    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = SD_PIN_CS;
    slot_cfg.host_id = SPI2_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = SD_MAX_FILES,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    ESP_LOGI(TAG, "Mounting SD (CS=%d, shared SPI2)...", SD_PIN_CS);
    esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_cfg,
                                            &mount_cfg, &s_card);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "SD not mounted: %s", esp_err_to_name(ret));
        return ret;
    }
    s_mounted = true;
    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "SD ready at %s", SD_MOUNT_POINT);
    return ESP_OK;
}

bool sdcard_is_mounted(void) { return s_mounted; }

void sdcard_list_gcode_files(sdcard_file_cb_t cb, void *arg)
{
    if (!s_mounted || !cb)
        return;
    DIR *dir = opendir(SD_MOUNT_POINT);
    if (!dir)
        return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type != DT_REG)
            continue;
        const char *name = entry->d_name;
        int len = strlen(name);
        bool ok = false;
        if (len > 3 && strcasecmp(name + len - 3, ".nc") == 0)
            ok = true;
        if (len > 4 && strcasecmp(name + len - 4, ".gco") == 0)
            ok = true;
        if (len > 4 && strcasecmp(name + len - 4, ".gcd") == 0)
            ok = true;
        if (len > 4 && strcasecmp(name + len - 4, ".txt") == 0)
            ok = true;
        if (len > 2 && strcasecmp(name + len - 2, ".g") == 0)
            ok = true;
        if (!ok)
            continue;

        size_t needed = strlen(SD_MOUNT_POINT) + 1 + len + 1; // mount + '/' + name + '\0'
        char *path = malloc(needed);
        if (!path)
            continue;
        size_t mlen = strlen(SD_MOUNT_POINT);
        memcpy(path, SD_MOUNT_POINT, mlen);
        path[mlen] = '/';
        memcpy(path + mlen + 1, name, len);
        path[mlen + 1 + len] = '\0';
        struct stat st;
        uint32_t size = (stat(path, &st) == 0) ? (uint32_t)st.st_size : 0;
        cb(name, size, arg);
        free(path);
    }
    closedir(dir);
}

int sdcard_read_file(const char *filename, char *buf, int max_len)
{
    if (!s_mounted || !filename || !buf || max_len <= 0)
        return 0;
    size_t flen = strlen(filename);
    size_t needed = strlen(SD_MOUNT_POINT) + 1 + flen + 1;
    char *path = malloc(needed);
    if (!path)
        return 0;
    size_t mlen = strlen(SD_MOUNT_POINT);
    memcpy(path, SD_MOUNT_POINT, mlen);
    path[mlen] = '/';
    memcpy(path + mlen + 1, filename, flen);
    path[mlen + 1 + flen] = '\0';
    FILE *f = fopen(path, "r");
    if (!f)
    {
        free(path);
        return 0;
    }
    int total = 0;
    while (total < max_len - 1)
    {
        int n = fread(buf + total, 1, max_len - 1 - total, f);
        if (n <= 0)
            break;
        total += n;
    }
    buf[total] = '\0';
    fclose(f);
    free(path);
    return total;
}

void sdcard_info(uint32_t *total_mb, uint32_t *free_mb)
{
    if (!s_mounted || !s_card)
    {
        if (total_mb)
            *total_mb = 0;
        if (free_mb)
            *free_mb = 0;
        return;
    }
    FATFS *fs = NULL;
    DWORD fc = 0;
    if (f_getfree("0:", &fc, &fs) == FR_OK && fs)
    {
        if (total_mb)
            *total_mb = (uint32_t)((fs->n_fatent - 2) * fs->csize * 512 / (1024 * 1024));
        if (free_mb)
            *free_mb = (uint32_t)(fc * fs->csize * 512 / (1024 * 1024));
    }
}