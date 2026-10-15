# ESP32-4848S040 hardware contract

The touchscreen target is the GUITION / Jingcai ESP32-4848S040: ESP32-S3, 16 MB flash, 8 MB octal PSRAM, 480 × 480 ST7701 RGB LCD and GT911 capacitive touch. The display occupies the RGB and three-wire control pins below. Do not reuse them for indicators or peripherals.

| Signal | GPIO |
| --- | --- |
| LCD control CS / SCK / SDA | 39 / 48 / 47 |
| RGB HSYNC / VSYNC / DE / PCLK | 16 / 17 / 18 / 21 |
| RGB data D0–D4 | 4 / 5 / 6 / 7 / 15 |
| RGB data D5–D10 | 8 / 20 / 3 / 46 / 9 / 10 |
| RGB data D11–D15 | 11 / 12 / 13 / 14 / 0 |
| GT911 SDA / SCL | 19 / 45 |
| Backlight, active high | 38 |

Use the maintained `BOARD_JINGCAI_ESP32_4848S040C_I_Y_3` display profile from ESP32_Display_Panel 1.0.4. Its ST7701 initialization sequence, GPIO mapping, blanking intervals and GT911 address handling form the hardware configuration. The application overrides the pixel clock to 16 MHz: 520 clocks per line and 510 lines per frame produce about 60.3 Hz. The profile default of 26 MHz requests about 98 Hz and consumes 45.2 MB/s of framebuffer reads; 16 MHz reduces that scanout traffic to 27.8 MB/s, leaving more shared PSRAM bandwidth for rendering and HTTPS. Do not substitute a generic ST7701 initialization sequence. The application does not operate the optional relays.

The SDK must use a 64 KB data cache and 64-byte cache lines. Disable unconditional RGB DMA restart at VSYNC; the driver still recovers real underruns. Enable PSRAM instruction/read-only-data placement and cache-safe RGB interrupts so NVS writes do not interrupt scanout. These are SDK build settings, not sketch macros. The supported build uses `firmware/sdkconfig.defaults`; startup source guards reject an incompatible prebuilt SDK.

The dependency versions are recorded in [toolchain.lock.json](../toolchain.lock.json). Install libraries into the repository's ignored `build/arduino/libraries` directory; do not overwrite the user's Arduino sketchbook libraries.

## Evidence

- [GUITION model list](https://www.guition.com/model-selection): memory, screen resolution and board family.
- [Espressif board profile at v1.0.4](https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/v1.0.4/src/board/supported/jingcai/BOARD_JINGCAI_ESP32_4848S040C_I_Y_3.h): display initialization, pins, touch and backlight.
- [ESP-IDF RGB framebuffer modes](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html): framebuffer synchronization, PSRAM bandwidth and bounce-buffer constraints.
- [Espressif Jingcai board guide](https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/v1.0.4/docs/board/board_jingcai.md): board selection and Arduino configuration.

## Device acceptance checks

After flashing, verify all four corners respond at their displayed coordinates, text has correct colors, scrolling does not shift the RGB scanout, and touch remains responsive during HTTPS requests. Reboot to verify Wi-Fi, server, read token and four panel selections persist. Test an unavailable access point and a rejected token separately. A successful compile or host simulation does not prove these physical checks.

For a release that changes update behavior, also verify on the device: publish a newer test release, check that settings show the install progress, the device restarts into the new version and `info` reports the other `firmwareSlot`. Then install an image that cannot reach Wi-Fi, restart it and check that the previous version starts again.
