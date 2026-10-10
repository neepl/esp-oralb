// Pin map and board constants for the Sunton ESP32-2432S024C (2.4" 240x320, capacitive touch).
// Verified against rzeldent/platformio-espressif32-sunton, esp32-2432S024C.json:
//   https://github.com/rzeldent/platformio-espressif32-sunton/blob/main/esp32-2432S024C.json
#pragma once

#define BOARD_TFT_MOSI 13
#define BOARD_TFT_MISO 12
#define BOARD_TFT_SCLK 14
#define BOARD_TFT_CS 15
#define BOARD_TFT_DC 2
#define BOARD_TFT_RST (-1)
#define BOARD_TFT_BL 27

// Touch: CST816S family, I2C address 0x15 (SDA 33 / SCL 32, INT 21, RST 25)
#define BOARD_TOUCH_SDA 33
#define BOARD_TOUCH_SCL 32
#define BOARD_TOUCH_INT 21
#define BOARD_TOUCH_RST 25
#define BOARD_TOUCH_ADDR 0x15

#define SCREEN_W 240
#define SCREEN_H 320

// The vendor definition sets MADCTL mirror-X for portrait; flip these if the image is mirrored.
#ifndef BOARD_TFT_OFFSET_ROTATION
#define BOARD_TFT_OFFSET_ROTATION 0
#endif
#ifndef BOARD_TFT_RGB_ORDER_BGR
#define BOARD_TFT_RGB_ORDER_BGR 0
#endif
#ifndef BOARD_TFT_INVERT
#define BOARD_TFT_INVERT 0
#endif

// Landscape UI. SCREEN_W/SCREEN_H above are the native (portrait) panel size;
// UI_W/UI_H are what the application draws in after rotation.
// 1 = landscape (USB connector on the left side...), 3 = the same rotated by 180 degrees.
#ifndef DISPLAY_ROTATION
#define DISPLAY_ROTATION 1
#endif
#define UI_W 320
#define UI_H 240

// Other on-board peripherals (rzeldent esp32-2432S024C.json): RGB LED, CdS light sensor.
// GPIO26 drives the speaker amplifier; unused (no speaker fitted on this unit).
#define BOARD_LED_R 4
#define BOARD_LED_G 16
#define BOARD_LED_B 17
#define BOARD_LDR 34      // analog, input only
