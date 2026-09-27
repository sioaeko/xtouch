#ifndef _XLCD_TOUCH
#define _XLCD_TOUCH

#include <XPT2046_Touchscreen.h>

#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

SPIClass x_touch_spi = SPIClass(HSPI);
XPT2046_Touchscreen x_touch_touchScreen(XPT2046_CS, XPT2046_IRQ);
XTouchPanelConfig x_touch_touchConfig;
bool xtouch_touch_ready = false;
void xtouch_webserver_loop();

void xtouch_touch_wait(bool pressed)
{
    while (x_touch_touchScreen.touched() == pressed)
    {
        xtouch_webserver_loop();
        delay(5);
    }
}

class ScreenPoint
{
public:
    int16_t x;
    int16_t y;

    // default constructor
    ScreenPoint()
    {
    }

    ScreenPoint(int16_t xIn, int16_t yIn)
    {
        x = xIn;
        y = yIn;
    }
};

ScreenPoint getScreenCoords(int16_t x, int16_t y)
{
    int16_t xCoord = round((x * x_touch_touchConfig.xCalM) + x_touch_touchConfig.xCalC);
    int16_t yCoord = round((y * x_touch_touchConfig.yCalM) + x_touch_touchConfig.yCalC);
    if (xCoord < 0)
        xCoord = 0;
    if (xCoord >= 320)
        xCoord = 320 - 1;
    if (yCoord < 0)
        yCoord = 0;
    if (yCoord >= 240)
        yCoord = 240 - 1;
    return (ScreenPoint(xCoord, yCoord));
}

void xtouch_loadTouchConfig(XTouchPanelConfig &config)
{
    // Open file for reading
    File file = xtouch_filesystem_open(SPIFFS, xtouch_paths_touch);

    // Allocate a temporary JsonDocument
    // Don't forget to change the capacity to match your requirements.
    // Use arduinojson.org/v6/assistant to compute the capacity.
    StaticJsonDocument<512> doc;

    // Deserialize the JSON document
    DeserializationError error = deserializeJson(doc, file);
    if (error)
        ConsoleError.println(F("[XTouch][Touch] Failed to read touch config"));

    config.xCalM = doc["xCalM"].as<float>();
    config.yCalM = doc["yCalM"].as<float>();
    config.xCalC = doc["xCalC"].as<float>();
    config.yCalC = doc["yCalC"].as<float>();

    file.close();
}

void xtouch_saveTouchConfig(XTouchPanelConfig &config)
{
    StaticJsonDocument<512> doc;
    doc["xCalM"] = config.xCalM;
    doc["yCalM"] = config.yCalM;
    doc["xCalC"] = config.xCalC;
    doc["yCalC"] = config.yCalC;
    xtouch_filesystem_writeJson(SPIFFS, xtouch_paths_touch, doc);
}

void xtouch_resetTouchConfig()
{
    ConsoleInfo.println(F("[XTouch][FS] Resetting touch config"));
    xtouch_filesystem_deleteFile(SPIFFS, xtouch_paths_touch);
    delay(500);
    ESP.restart();
}

bool hasTouchConfig()
{
    ConsoleInfo.println(F("[XTouch][FS] Checking for touch config"));
    return xtouch_filesystem_exist(SPIFFS, xtouch_paths_touch);
}

void xtouch_touch_setup()
{
    if (hasTouchConfig())
    {
        ConsoleInfo.println(F("[XTouch][TOUCH] Load from disk"));
        xtouch_loadTouchConfig(x_touch_touchConfig);
    }
    else
    {
        ConsoleInfo.println(F("[XTouch][TOUCH] Touch Setup"));
        TS_Point p;
        int16_t x1, y1, x2, y2;

        lv_label_set_text(introScreenCaption, "Touch the  " LV_SYMBOL_PLUS "  with the stylus");
        lv_timer_handler();

        // wait for no touch
        xtouch_touch_wait(true);
        tft.drawFastHLine(0, 10, 20, ILI9341_WHITE);
        tft.drawFastVLine(10, 0, 20, ILI9341_WHITE);
        xtouch_touch_wait(false);
        delay(50);
        p = x_touch_touchScreen.getPoint();
        x1 = p.x;
        y1 = p.y;
        tft.drawFastHLine(0, 10, 20, ILI9341_BLACK);
        tft.drawFastVLine(10, 0, 20, ILI9341_BLACK);
        delay(500);

        xtouch_touch_wait(true);
        tft.drawFastHLine(300, 230, 20, ILI9341_WHITE);
        tft.drawFastVLine(310, 220, 20, ILI9341_WHITE);

        xtouch_touch_wait(false);
        delay(50);
        p = x_touch_touchScreen.getPoint();
        x2 = p.x;
        y2 = p.y;
        tft.drawFastHLine(300, 230, 20, ILI9341_BLACK);
        tft.drawFastVLine(310, 220, 20, ILI9341_BLACK);

        if (abs(x2 - x1) < 100 || abs(y2 - y1) < 100)
        {
            ConsoleError.println("[XTouch][TOUCH] Invalid calibration; restarting");
            delay(500);
            ESP.restart();
        }
        int16_t xDist = 300;
        int16_t yDist = 220;

        x_touch_touchConfig.xCalM = (float)xDist / (float)(x2 - x1);
        x_touch_touchConfig.xCalC = 10.0 - ((float)x1 * x_touch_touchConfig.xCalM);
        // y
        x_touch_touchConfig.yCalM = (float)yDist / (float)(y2 - y1);
        x_touch_touchConfig.yCalC = 10.0 - ((float)y1 * x_touch_touchConfig.yCalM);

        xtouch_saveTouchConfig(x_touch_touchConfig);

        loadScreen(-1);
    }
    xtouch_touch_ready = true;
}

#endif
