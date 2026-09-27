#pragma once

// Force the same CYD wiring into the application AND every TFT_eSPI source.
// An include search path alone lets the library's default User_Setup.h win.
#define USER_SETUP_INFO "XTouch CYD ESP32-2432S028R"
#define ILI9341_2_DRIVER
#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST -1
#define TFT_BL 21
#define TFT_BACKLIGHT_ON HIGH
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 20000000
// Touch has its own XPT2046 driver on HSPI. Do not enable TFT_eSPI touch.
