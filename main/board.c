// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// Screen and touch for the Waveshare ESP32-S3-Touch-AMOLED-1.75.
//
// The pins, the CO5300 start-up commands and the brightness command come from Waveshare's
// board package (waveshare/esp32_s3_touch_amoled_1_75, Apache-2.0, Copyright Waveshare).
// We don't use that package itself: it pulls in a library whose licence GPL-3.0 doesn't allow
// (see main/idf_component.yml).

#include "board.h"

#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "board";

// Screen: CO5300 AMOLED over QSPI.
#define LCD_SPI_HOST SPI2_HOST
#define LCD_PIN_CS 12
#define LCD_PIN_CLK 38
#define LCD_PIN_D0 4
#define LCD_PIN_D1 5
#define LCD_PIN_D2 6
#define LCD_PIN_D3 7
#define LCD_PIN_RESET 39
#define LCD_BITS_PER_PIXEL 16 // RGB565
// The panel's visible pixels start 6 columns into the CO5300's memory.
#define LCD_COLUMN_OFFSET 6

// Touch: CST9217 on I2C, shared with the power chip and the IMU.
#define I2C_PORT I2C_NUM_1
#define I2C_PIN_SDA 15
#define I2C_PIN_SCL 14
#define I2C_SPEED_HZ 100000
#define TOUCH_PIN_RESET 40
#define TOUCH_PIN_INT 11

// LVGL draws in bands of this many lines, two buffers so one is sent while the next is drawn.
#define DRAW_BUFFER_LINES 50

// The CO5300 brightness command (0x51), wrapped the way the QSPI panel IO expects a command.
#define LCD_CMD_BRIGHTNESS ((0x02 << 24) | (0x51 << 8))

// Start-up commands for this panel, from Waveshare's board package.
static const co5300_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0}, // 16 bits a pixel
    {0x35, (uint8_t[]){0x00}, 1, 0}, // tearing signal on
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0}, // full brightness
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},   // columns 6 to 471
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600}, // rows 0 to 465
    {0x11, NULL, 0, 600},                                // wake from sleep
    {0x29, NULL, 0, 0},                                  // display on
};

static esp_lcd_panel_io_handle_t lcd_io;
static esp_lcd_panel_handle_t lcd_panel;

// The CO5300 only takes areas that start on an even pixel and end on an odd one.
static void round_to_even(lv_area_t *area)
{
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

static esp_err_t start_screen(void)
{
    const spi_bus_config_t bus = CO5300_PANEL_BUS_QSPI_CONFIG(
        LCD_PIN_CLK, LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3,
        BOARD_SCREEN_PX * DRAW_BUFFER_LINES * LCD_BITS_PER_PIXEL / 8);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "SPI bus");

    const esp_lcd_panel_io_spi_config_t io = CO5300_PANEL_IO_QSPI_CONFIG(LCD_PIN_CS, NULL, NULL);
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io, &lcd_io),
                        TAG, "panel IO");

    co5300_vendor_config_t vendor = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {.use_qspi_interface = 1},
    };
    const esp_lcd_panel_dev_config_t panel = {
        .reset_gpio_num = LCD_PIN_RESET,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
        .vendor_config = &vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(lcd_io, &panel, &lcd_panel), TAG, "panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(lcd_panel, LCD_COLUMN_OFFSET, 0), TAG, "gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(lcd_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(lcd_panel), TAG, "init");
    return esp_lcd_panel_disp_on_off(lcd_panel, true);
}

static esp_err_t start_touch(esp_lcd_touch_handle_t *touch)
{
    i2c_master_bus_handle_t i2c;
    const i2c_master_bus_config_t bus = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_PIN_SDA,
        .scl_io_num = I2C_PIN_SCL,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &i2c), TAG, "I2C bus");

    esp_lcd_panel_io_handle_t touch_io;
    esp_lcd_panel_io_i2c_config_t io = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    io.scl_speed_hz = I2C_SPEED_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c, &io, &touch_io), TAG, "touch IO");

    const esp_lcd_touch_config_t config = {
        .x_max = BOARD_SCREEN_PX,
        .y_max = BOARD_SCREEN_PX,
        .rst_gpio_num = TOUCH_PIN_RESET,
        .int_gpio_num = TOUCH_PIN_INT,
        .levels = {.reset = 0, .interrupt = 0},
        // The touch panel is mounted turned half a turn from the screen.
        .flags = {.swap_xy = 0, .mirror_x = 1, .mirror_y = 1},
    };
    return esp_lcd_touch_new_i2c_cst9217(touch_io, &config, touch);
}

lv_display_t *board_start(void)
{
    if (start_screen() != ESP_OK) return NULL;

    // LVGL's task draws on core 1; core 0 is kept for Wi-Fi and the link to the phone.
    lvgl_port_cfg_t lvgl = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl.task_affinity = 1;
    if (lvgl_port_init(&lvgl) != ESP_OK) return NULL;

    const lvgl_port_display_cfg_t display_config = {
        .io_handle = lcd_io,
        .panel_handle = lcd_panel,
        .buffer_size = BOARD_SCREEN_PX * DRAW_BUFFER_LINES,
        .double_buffer = true,
        .hres = BOARD_SCREEN_PX,
        .vres = BOARD_SCREEN_PX,
        .rounder_cb = round_to_even,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,   // in internal memory, so the SPI can send it straight away
            .swap_bytes = true, // the panel expects each pixel's high byte first
        },
    };
    lv_display_t *display = lvgl_port_add_disp(&display_config);
    if (!display) return NULL;

    // Without touch the screen still works, so this is logged rather than fatal.
    esp_lcd_touch_handle_t touch = NULL;
    if (start_touch(&touch) == ESP_OK) {
        const lvgl_port_touch_cfg_t touch_config = {.disp = display, .handle = touch};
        lvgl_port_add_touch(&touch_config);
    } else {
        ESP_LOGE(TAG, "Touch didn't start");
    }

    ESP_LOGI(TAG, "Screen started");
    return display;
}

esp_err_t board_set_brightness(uint8_t level)
{
    return esp_lcd_panel_io_tx_param(lcd_io, LCD_CMD_BRIGHTNESS, &level, 1);
}
