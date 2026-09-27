#ifndef _XLCD_WEBSERVER
#define _XLCD_WEBSERVER

#include <WebServer.h>
#include "config.h"

extern const char xtouch_setup_html[] asm("_binary_setup_index_html_start");
WebServer xtouch_webServer(80);
unsigned long xtouch_webserver_restart_at = 0;
bool xtouch_webserver_started = false;
bool xtouch_setup_ap_active = false;
String xtouch_setup_ssid;
String xtouch_setup_password;
String xtouch_serial_line;
bool xtouch_serial_overflow = false;

void xtouch_webserver_send_json(int statusCode, const char *body)
{
    xtouch_webServer.sendHeader("Access-Control-Allow-Origin", "*");
    xtouch_webServer.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    xtouch_webServer.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    xtouch_webServer.sendHeader("Access-Control-Allow-Private-Network", "true");
    xtouch_webServer.sendHeader("Cache-Control", "no-store");
    xtouch_webServer.send(statusCode, "application/json", body);
}

bool xtouch_webserver_save_config(const JsonDocument &config)
{
    File pending = SPIFFS.open(xtouch_paths_config_pending, FILE_WRITE);
    if (!pending) return false;
    const size_t expected = measureJson(config);
    const size_t written = serializeJson(config, pending);
    pending.flush();
    const size_t size = pending.size();
    pending.close();
    if (written != expected || size != expected)
    {
        SPIFFS.remove(xtouch_paths_config_pending);
        return false;
    }
    const bool hadConfig = SPIFFS.exists(xtouch_paths_config);
    if (hadConfig)
    {
        if (SPIFFS.exists(xtouch_paths_config_backup) && !SPIFFS.remove(xtouch_paths_config_backup)) return false;
        if (!SPIFFS.rename(xtouch_paths_config, xtouch_paths_config_backup)) return false;
    }
    if (!SPIFFS.rename(xtouch_paths_config_pending, xtouch_paths_config))
    {
        if (hadConfig) SPIFFS.rename(xtouch_paths_config_backup, xtouch_paths_config);
        return false;
    }
    return true;
}

// HTTP and USB share exactly the same validation and atomic storage path.
const char *xtouch_provision_body(const String &body, bool save)
{
    if (xtouch_webserver_restart_at != 0) return "restart already pending";
    if (body.length() > XTOUCH_CONFIG_CAPACITY) return "configuration is too large";
    DynamicJsonDocument config(XTOUCH_CONFIG_CAPACITY);
    if (deserializeJson(config, body)) return "invalid JSON";
    const char *error = xtouch_config_normalize(config);
    if (!error) error = xtouch_config_validate(config.as<JsonObjectConst>());
    if (error) return error;
    if (save && !xtouch_webserver_save_config(config)) return "failed to write internal configuration";
    return nullptr;
}

void xtouch_webserver_handle_provision()
{
    if (!xtouch_mqtt_needs_provisioning || xtouch_webserver_restart_at != 0)
    {
        xtouch_webserver_send_json(409, "{\"error\":\"provisioning is not active on the screen\"}");
        return;
    }
    const char *error = xtouch_webServer.hasArg("plain") ?
        xtouch_provision_body(xtouch_webServer.arg("plain"), true) : "missing request body";
    if (error)
    {
        StaticJsonDocument<192> response;
        response["error"] = error;
        String json;
        serializeJson(response, json);
        xtouch_webserver_send_json(400, json.c_str());
        return;
    }
    xtouch_webserver_send_json(200, "{\"status\":\"ok\",\"restart_in_ms\":1500}");
    xtouch_webserver_restart_at = millis() + 1500;
}

void xtouch_setup_start_ap()
{
    if (!xtouch_setup_ap_active)
    {
        char name[32], password[16];
        snprintf(name, sizeof(name), "XTouch-Setup-%04X", unsigned((ESP.getEfuseMac() >> 32) & 0xffff));
        snprintf(password, sizeof(password), "%08lx%04x", (unsigned long)esp_random(), unsigned(esp_random() & 0xffff));
        xtouch_setup_ssid = name;
        xtouch_setup_password = password;
        WiFi.setAutoReconnect(false);
        WiFi.mode(WIFI_AP);
        xtouch_setup_ap_active = WiFi.softAP(name, password);
        ConsoleInfo.printf("[XTouch][SETUP] WiFi: %s; password: %s; open http://192.168.4.1\n", name, password);
    }
    xtouch_mqtt_needs_provisioning = true;
    if (xTouchConfig.currentScreenIndex != -1) loadScreen(-1);
    if (xtouch_screen_onScreenOffTimer) lv_timer_pause(xtouch_screen_onScreenOffTimer);
    lv_obj_add_flag(introScreenIcon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(introScreenCaption, lv_color_hex(0xeeeeee), 0);
    lv_label_set_text_fmt(introScreenCaption, "Setup without SD\n\nWiFi: %s\nPassword: %s\n\nhttp://192.168.4.1\nOr use USB setup on your PC", xtouch_setup_ssid.c_str(), xtouch_setup_password.c_str());
    xtouch_screen_setBrightness(180);
    lv_timer_handler();
}

void xtouch_serial_result(const char *error = nullptr)
{
    StaticJsonDocument<256> response;
    response["status"] = error ? "error" : "ok";
    if (error) response["error"] = error;
    Serial.print("XTOUCH_RESULT ");
    serializeJson(response, Serial);
    Serial.println();
}

void xtouch_serial_command(const String &line)
{
    if (line == "XTOUCH STATUS")
    {
        StaticJsonDocument<1536> response;
        response["status"] = "ok";
        response["version"] = XTOUCH_FIRMWARE_VERSION;
        response["storage"] = "internal";
        response["storage_bytes"] = SPIFFS.totalBytes();
        response["config_present"] = SPIFFS.exists(xtouch_paths_config) || SPIFFS.exists(xtouch_paths_config_backup);
        response["wifi_connected"] = WiFi.status() == WL_CONNECTED;
        response["mqtt_connected"] = xtouch_pubSubClient.connected();
        response["mqtt_state"] = xtouch_pubSubClient.state();
        response["light_on"] = bambuStatus.chamberLed;
        response["light_requests"] = xtouch_light_requests;
        response["light_target"] = xtouch_light_last_target;
        response["light_published"] = xtouch_light_last_published;
        response["light_result"] = xtouch_light_result;
        response["printer_status_received"] = xtouch_mqtt_firstConnectionDone;
        response["screen_index"] = xTouchConfig.currentScreenIndex;
        if (xtouch_mqtt_firstConnectionDone)
        {
            response["printer_firmware"] = bambuStatus.printer_firmware;
            response["native_filament_supported"] = bambuStatus.native_filament_supported;
            response["ams_tray_now"] = bambuStatus.m_tray_now;
            response["filament_can_load"] = xtouch_can_load_filament();
            response["filament_can_unload"] = xtouch_can_unload_filament();
            response["status_age_ms"] = millis() - xtouch_mqtt_lastPushStatus;
            response["nozzle_temperature"] = bambuStatus.nozzle_temper;
            response["bed_temperature"] = bambuStatus.bed_temper;
        }
        response["ip"] = WiFi.localIP().toString();
        response["setup_ap"] = xtouch_setup_ap_active;
        if (xtouch_setup_ap_active)
        {
            response["ap_ssid"] = xtouch_setup_ssid;
            response["ap_password"] = xtouch_setup_password; // Local USB only; never exposes saved credentials.
        }
        Serial.print("XTOUCH_RESULT ");
        serializeJson(response, Serial);
        Serial.println();
    }
    else if (line == "XTOUCH LIGHT TOGGLE")
    {
        if (!xtouch_pubSubClient.connected() || !xtouch_mqtt_firstConnectionDone)
        {
            xtouch_serial_result("printer is not connected");
            return;
        }
        // Local USB diagnosis uses the same subscription as the touch button.
        lv_msg_send(XTOUCH_COMMAND_LIGHT_TOGGLE, nullptr);
        xtouch_serial_result(xtouch_light_last_published ? nullptr : "light publish failed");
    }
    else if (line == "XTOUCH SETUP")
    {
        xtouch_setup_start_ap();
        xtouch_serial_result();
    }
    else if (line.startsWith("XTOUCH PROVISION ") || line.startsWith("XTOUCH VALIDATE "))
    {
        const bool save = line.startsWith("XTOUCH PROVISION ");
        const char *error = xtouch_provision_body(line.substring(save ? 17 : 16), save);
        xtouch_serial_result(error);
        if (!error && save) xtouch_webserver_restart_at = millis() + 1500;
    }
    else xtouch_serial_result("unknown command");
}

void xtouch_serial_loop()
{
    // Bound both memory and work per frame. Never echo a received credential.
    for (int processed = 0; processed < 512 && Serial.available(); ++processed)
    {
        const char c = Serial.read();
        if (c == '\r') continue;
        if (c == '\n')
        {
            if (xtouch_serial_overflow) xtouch_serial_result("configuration is too large");
            else if (xtouch_serial_line.length()) xtouch_serial_command(xtouch_serial_line);
            xtouch_serial_line = "";
            xtouch_serial_overflow = false;
        }
        else if (!xtouch_serial_overflow)
        {
            if (xtouch_serial_line.length() >= XTOUCH_CONFIG_CAPACITY + 32)
            {
                xtouch_serial_line = "";
                xtouch_serial_overflow = true;
            }
            else xtouch_serial_line += c;
        }
    }
}

void xtouch_webserver_begin()
{
    if (xtouch_webserver_started) return;
    xtouch_mqtt_needs_provisioning = true;
    xtouch_serial_line.reserve(256);
    xtouch_webServer.on("/", HTTP_GET, []() {
        xtouch_webServer.sendHeader("Cache-Control", "no-store");
        xtouch_webServer.send_P(200, "text/html; charset=utf-8", xtouch_setup_html);
    });
    xtouch_webServer.on("/provision", HTTP_POST, xtouch_webserver_handle_provision);
    xtouch_webServer.on("/provision", HTTP_OPTIONS, []() { xtouch_webserver_send_json(204, ""); });
    xtouch_webServer.onNotFound([]() { xtouch_webserver_send_json(404, "{\"error\":\"not found\"}"); });
    xtouch_webServer.begin();
    xtouch_webserver_started = true;
    ConsoleInfo.println("[XTouch][SETUP] HTTP and USB provisioning ready; internal flash storage");
}

void xtouch_webserver_loop()
{
    xtouch_serial_loop();
    if (xtouch_webserver_started) xtouch_webServer.handleClient();
    if (xtouch_webserver_restart_at != 0 &&
        static_cast<int32_t>(millis() - xtouch_webserver_restart_at) >= 0) ESP.restart();
}

#endif
