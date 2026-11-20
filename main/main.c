// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// AstroTurboMaxiPuck firmware entry point.
//
// For now this is only a bring-up check: it prints what the chip reports
// (cores, flash, PSRAM) so we can confirm the board and the sdkconfig match
// the Waveshare ESP32-S3-Touch-AMOLED-1.75 (16 MB flash, 8 MB PSRAM).
// Screen, touch and battery come next.

#include <inttypes.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "puck";

// Print the chip, flash and PSRAM details once at start-up.
static void log_board_info(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "Chip: ESP32-S3 rev %d.%d, %d cores",
             chip.revision / 100, chip.revision % 100, chip.cores);

    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        ESP_LOGI(TAG, "Flash: %" PRIu32 " MB", flash_size / (1024 * 1024));
    }

    ESP_LOGI(TAG, "PSRAM: %u MB", (unsigned)(esp_psram_get_size() / (1024 * 1024)));
    ESP_LOGI(TAG, "Free heap: internal %u KB, PSRAM %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

void app_main(void)
{
    ESP_LOGI(TAG, "AstroTurboMaxiPuck starting");
    log_board_info();

    // Heartbeat, so the serial monitor shows the firmware is alive.
    for (int seconds = 0;; seconds += 5) {
        ESP_LOGI(TAG, "Alive for %d s", seconds);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
