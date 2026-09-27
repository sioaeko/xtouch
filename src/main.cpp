#include <driver/i2s.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include "xtouch/debug.h"
#include "xtouch/paths.h"
#include "xtouch/eeprom.h"
#include "xtouch/types.h"
#include "xtouch/bblp.h"
#include "xtouch/globals.h"
#include "xtouch/filesystem.h"
#include "ui/ui.h"
#include "xtouch/sdcard.h"
#include "xtouch/hms.h"

#if defined(__XTOUCH_SCREEN_28__)
#include "devices/2.8/screen.h"
#endif

#include "xtouch/settings.h"
#include "xtouch/net.h"
#include "xtouch/firmware.h"
#include "xtouch/mqtt.h"
#include "xtouch/sensors/chamber.h"
#include "xtouch/events.h"
#include "xtouch/connection.h"
#include "xtouch/coldboot.h"
#include "xtouch/webserver.h"

bool xtouch_runtime_ready = false;

void xtouch_intro_show(void)
{
  xTouchConfig.currentScreenIndex = -1;
  ui_introScreen_screen_init();
  lv_disp_load_scr(introScreen);
  lv_timer_handler();
}

void setup()
{

#if XTOUCH_USE_SERIAL == true || XTOUCH_DEBUG_ERROR == true || XTOUCH_DEBUG_DEBUG == true || XTOUCH_DEBUG_INFO == true
  Serial.setRxBufferSize(XTOUCH_CONFIG_CAPACITY + 128);
  Serial.begin(115200);
  ConsoleInfo.printf("[XTouch] CYD Cloud revision %s\n", XTOUCH_FIRMWARE_VERSION);
#endif

  xtouch_eeprom_setup();
  xtouch_globals_init();
  if (xtouch_sdcard_setup())
  {
    xtouch_sdcard_import();
    xtouch_firmware_checkFirmwareUpdate();
    xtouch_sdcard_end();
  }
  xtouch_screen_setup();
  xtouch_intro_show();

  xtouch_coldboot_check();

  xtouch_settings_loadSettings();

  WiFi.mode(WIFI_STA);
  xtouch_webserver_begin();
  if (!xtouch_wifi_setup())
  {
    xtouch_setup_start_ap();
    return;
  }
  if (xtouch_config_error != nullptr)
  {
    xtouch_mqtt_show_provisioning("Check Cloud settings");
    return;
  }
  xtouch_touch_setup();

  xtouch_firmware_checkOnlineFirmwareUpdate();

  xtouch_screen_setupScreenTimer();
  xtouch_setupGlobalEvents();

  xtouch_mqtt_setup();
  xtouch_chamber_timer_init();
  xtouch_runtime_ready = true;
}

void loop()
{
  lv_timer_handler();
  lv_task_handler();
  xtouch_webserver_loop();
  if (xtouch_runtime_ready && !xtouch_setup_ap_active && xtouch_webserver_restart_at == 0)
    xtouch_mqtt_loop();
  delay(2);
}
