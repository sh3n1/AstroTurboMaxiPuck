# Third-party code

The firmware is GPL-3.0-only. It uses these libraries, all GPL-3.0 compatible. They are
downloaded from the ESP Component Registry at build time (see `main/idf_component.yml` and
`dependencies.lock`), each with its own licence file in `managed_components/`.

| Library                                  | Used for                       | Licence    | Copyright        |
| ---------------------------------------- | ------------------------------ | ---------- | ---------------- |
| ESP-IDF                                  | The framework                  | Apache-2.0 | Espressif        |
| `espressif/esp_lcd_co5300`               | CO5300 screen driver           | Apache-2.0 | Espressif        |
| `waveshare/esp_lcd_touch_cst9217`        | CST9217 touch driver           | Apache-2.0 | Waveshare        |
| `espressif/esp_lcd_touch`                | Touch driver base              | Apache-2.0 | Espressif        |
| `espressif/esp_lvgl_port`                | LVGL on ESP-IDF                | Apache-2.0 | Espressif        |
| `espressif/cmake_utilities`              | Build helpers                  | Apache-2.0 | Espressif        |
| `lvgl/lvgl`                              | Drawing                        | MIT        | LVGL Kft         |

`main/board.c` takes its pins, the CO5300 start-up commands and the brightness command from
Waveshare's board package `waveshare/esp32_s3_touch_amoled_1_75` (Apache-2.0, Copyright
Waveshare).

## Not used

- **Waveshare's board package** (`waveshare/esp32_s3_touch_amoled_1_75`): it pulls in
  `espressif/esp_new_jpeg`, a closed-source library under the "ESPRESSIF MIT License", which
  only allows use on Espressif chips. GPL-3.0 allows neither the restriction nor the missing
  source.
