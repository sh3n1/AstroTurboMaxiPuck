// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The sky catalogue: see catalog.h.
//
// The "catalog" partition holds the catalogue's bytes from its start, so it can be
// memory-mapped as it is, and a small record in its last 4 KB sector: which catalogue it
// is, its size and CRC. The record is erased before a new catalogue is written and written
// only once the new one is checked, so a write cut short (a reset, a flat battery) leaves no
// half catalogue that looks whole.

#include "catalog.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "catalog";

#define PARTITION_SUBTYPE 0x40
#define PARTITION_LABEL "catalog"
#define SECTOR_BYTES 4096
#define RECORD_MAGIC 0x4B4D5441 // "ATMK", little-endian
// The writer runs on core 0, below the link task, so the link keeps its time.
#define WRITER_CORE 0
#define WRITER_PRIORITY 3
#define WRITER_STACK 4096

typedef struct {
    uint32_t magic;
    uint32_t bytes;
    uint32_t crc32;
    char id[CATALOG_ID_CHARS + 1];
} record_t;

static const esp_partition_t *partition;
static uint32_t record_offset;          // the last sector of the partition

// What is kept in flash: the id and the memory-mapped bytes.
static char kept_id[CATALOG_ID_CHARS + 1];
static const uint8_t *kept_data;
static size_t kept_bytes;
static esp_partition_mmap_handle_t kept_map;

// The transfer. Written by the link task and the writer, guarded by the lock.
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static catalog_state_t state = CATALOG_IDLE;
static uint8_t *incoming;               // in PSRAM
static uint32_t incoming_total, incoming_received, incoming_crc;
static char incoming_id[CATALOG_ID_CHARS + 1];
static const char *failure = "";
static TaskHandle_t writer;

// CRC-32 (IEEE, as zlib), as the app works it out (puck-catalog.ts).
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
    }
    crc = ~crc;
    for (size_t i = 0; i < length; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static void set_state(catalog_state_t new_state, const char *why)
{
    taskENTER_CRITICAL(&lock);
    state = new_state;
    if (why) failure = why;
    taskEXIT_CRITICAL(&lock);
}

static void free_incoming(void)
{
    heap_caps_free(incoming);
    incoming = NULL;
}

// Maps the kept catalogue, once its record says one is there.
static void map_kept(const record_t *record)
{
    const void *data;
    if (esp_partition_mmap(partition, 0, record->bytes, ESP_PARTITION_MMAP_DATA, &data, &kept_map) != ESP_OK) {
        ESP_LOGE(TAG, "Couldn't map the catalogue");
        return;
    }
    // The id and size before the data: the screen, on the other core, takes a catalogue as
    // there once kept_data is set.
    memcpy(kept_id, record->id, sizeof(kept_id));
    kept_bytes = record->bytes;
    kept_data = data;
}

static void unmap_kept(void)
{
    const uint8_t *was = kept_data;
    kept_data = NULL; // first, so the screen stops taking it
    if (was) esp_partition_munmap(kept_map);
    kept_bytes = 0;
    kept_id[0] = '\0';
}

// Writes the checked catalogue in PSRAM to flash, then maps it.
static bool write_incoming(void)
{
    // First the old record goes, so nothing half-written is ever taken as whole.
    unmap_kept();
    if (esp_partition_erase_range(partition, record_offset, SECTOR_BYTES) != ESP_OK) return false;

    uint32_t erase_bytes = (incoming_total + SECTOR_BYTES - 1) / SECTOR_BYTES * SECTOR_BYTES;
    ESP_LOGI(TAG, "Writing %u bytes", (unsigned)incoming_total);
    // A sector at a time, so the link task gets the cores in between.
    for (uint32_t at = 0; at < erase_bytes; at += SECTOR_BYTES) {
        if (esp_partition_erase_range(partition, at, SECTOR_BYTES) != ESP_OK) return false;
        uint32_t n = incoming_total - at < SECTOR_BYTES ? incoming_total - at : SECTOR_BYTES;
        if (esp_partition_write(partition, at, incoming + at, n) != ESP_OK) return false;
    }

    // Read back and checked before the record says it's there.
    uint32_t crc = 0;
    static uint8_t check[SECTOR_BYTES];
    for (uint32_t at = 0; at < incoming_total; at += SECTOR_BYTES) {
        uint32_t n = incoming_total - at < SECTOR_BYTES ? incoming_total - at : SECTOR_BYTES;
        if (esp_partition_read(partition, at, check, n) != ESP_OK) return false;
        crc = crc32_update(crc, check, n);
    }
    if (crc != incoming_crc) return false;

    record_t record = {.magic = RECORD_MAGIC, .bytes = incoming_total, .crc32 = incoming_crc};
    memcpy(record.id, incoming_id, sizeof(record.id));
    if (esp_partition_write(partition, record_offset, &record, sizeof(record)) != ESP_OK) return false;
    map_kept(&record);
    return true;
}

static void writer_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        bool ok = write_incoming();
        free_incoming();
        if (ok) {
            ESP_LOGI(TAG, "Catalogue %s kept", kept_id);
            set_state(CATALOG_DONE, NULL);
        } else {
            ESP_LOGE(TAG, "Couldn't write the catalogue");
            set_state(CATALOG_FAILED, "flash");
        }
    }
}

esp_err_t catalog_init(void)
{
    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, PARTITION_SUBTYPE, PARTITION_LABEL);
    if (!partition) {
        ESP_LOGE(TAG, "No catalogue partition: check partitions.csv");
        return ESP_ERR_NOT_FOUND;
    }
    record_offset = partition->size - SECTOR_BYTES;

    record_t record;
    if (esp_partition_read(partition, record_offset, &record, sizeof(record)) == ESP_OK &&
        record.magic == RECORD_MAGIC && record.bytes > 0 && record.bytes <= record_offset &&
        record.id[CATALOG_ID_CHARS] == '\0') {
        map_kept(&record);
        // Checked once at start: a flash that went bad shouldn't give a wrong sky.
        if (kept_data && crc32_update(0, kept_data, kept_bytes) != record.crc32) {
            ESP_LOGW(TAG, "The kept catalogue is damaged: it will be sent again");
            unmap_kept();
        }
    }
    if (kept_data) {
        ESP_LOGI(TAG, "Catalogue %s, %u bytes", kept_id, (unsigned)kept_bytes);
    } else {
        ESP_LOGI(TAG, "No catalogue kept yet");
    }

    BaseType_t ok = xTaskCreatePinnedToCore(writer_task, "catalog", WRITER_STACK, NULL,
                                            WRITER_PRIORITY, &writer, WRITER_CORE);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}

const char *catalog_kept_id(void)
{
    return kept_data ? kept_id : NULL;
}

bool catalog_begin(const char *id, uint32_t bytes, uint32_t crc32)
{
    catalog_state_t now = catalog_state(NULL, NULL);
    if (now == CATALOG_WRITING) return false; // still writing the last one
    free_incoming();
    if (strlen(id) != CATALOG_ID_CHARS || bytes == 0 || bytes > record_offset) {
        set_state(CATALOG_FAILED, "size");
        return false;
    }
    incoming = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!incoming) {
        set_state(CATALOG_FAILED, "memory");
        return false;
    }
    strcpy(incoming_id, id);
    incoming_total = bytes;
    incoming_received = 0;
    incoming_crc = crc32;
    ESP_LOGI(TAG, "Receiving catalogue %s, %u bytes", id, (unsigned)bytes);
    set_state(CATALOG_RECEIVING, NULL);
    return true;
}

void catalog_add_part(const uint8_t *data, size_t length)
{
    if (catalog_state(NULL, NULL) != CATALOG_RECEIVING) return;
    if (incoming_received + length > incoming_total) {
        free_incoming();
        set_state(CATALOG_FAILED, "size");
        return;
    }
    memcpy(incoming + incoming_received, data, length);
    taskENTER_CRITICAL(&lock);
    incoming_received += length;
    taskEXIT_CRITICAL(&lock);
    if (incoming_received < incoming_total) return;

    // All here: checked in PSRAM first, then written by the writer task.
    if (crc32_update(0, incoming, incoming_total) != incoming_crc) {
        ESP_LOGW(TAG, "The catalogue's CRC doesn't match");
        free_incoming();
        set_state(CATALOG_FAILED, "crc");
        return;
    }
    set_state(CATALOG_WRITING, NULL);
    xTaskNotifyGive(writer);
}

catalog_state_t catalog_state(uint32_t *received, uint32_t *total)
{
    taskENTER_CRITICAL(&lock);
    catalog_state_t now = state;
    if (received) *received = incoming_received;
    if (total) *total = incoming_total;
    taskEXIT_CRITICAL(&lock);
    return now;
}

const char *catalog_failure(void)
{
    return failure;
}

void catalog_acknowledge(void)
{
    catalog_state_t now = catalog_state(NULL, NULL);
    if (now == CATALOG_DONE || now == CATALOG_FAILED) set_state(CATALOG_IDLE, NULL);
}

void catalog_cancel(void)
{
    // A catalogue being written is finished anyway: it's whole and checked.
    if (catalog_state(NULL, NULL) != CATALOG_RECEIVING) return;
    free_incoming();
    set_state(CATALOG_IDLE, NULL);
}

void catalog_fail_receiving(const char *reason)
{
    if (catalog_state(NULL, NULL) != CATALOG_RECEIVING) return;
    free_incoming();
    set_state(CATALOG_FAILED, reason);
}

const uint8_t *catalog_data(size_t *bytes)
{
    if (catalog_state(NULL, NULL) == CATALOG_WRITING) return NULL; // being replaced
    if (bytes) *bytes = kept_bytes;
    return kept_data;
}
