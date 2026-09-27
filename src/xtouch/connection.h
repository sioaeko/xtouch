#ifndef _XLCD_CONNECTION
#define _XLCD_CONNECTION

void xtouch_webserver_loop();

bool xtouch_wifi_setup()
{
    DynamicJsonDocument config = xtouch_load_config();
    const char *ssid = config["ssid"] | "";
    if (!*ssid || strlen(ssid) > 32 || !config["pwd"].is<const char *>() ||
        strlen(config["pwd"].as<const char *>()) > 64) return false;

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, config["pwd"].as<const char *>());
    ConsoleInfo.println("[XTouch][WIFI] Connecting (20 second timeout)");
    lv_label_set_text(introScreenCaption, "Connecting to WiFi");
    const unsigned long started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 20000)
    {
        lv_timer_handler();
        xtouch_webserver_loop();
        delay(5);
    }
    if (WiFi.status() != WL_CONNECTED)
    {
        ConsoleInfo.println("[XTouch][WIFI] Connection failed; opening setup");
        return false;
    }
    ConsoleInfo.printf("[XTouch][WIFI] Connected: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

#endif
