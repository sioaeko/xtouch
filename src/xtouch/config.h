#ifndef _XLCD_JSONCONFIG
#define _XLCD_JSONCONFIG

#include "filesystem.h"
#include "paths.h"
#include "provisioning.h"

const char *xtouch_config_error = nullptr;

DynamicJsonDocument xtouch_load_config()
{
    const char *configPath = xtouch_paths_config;
    if (!SPIFFS.exists(configPath))
    {
        if (SPIFFS.exists(xtouch_paths_config_backup)) configPath = xtouch_paths_config_backup;
        else if (SPIFFS.exists(xtouch_paths_legacy_config)) configPath = xtouch_paths_legacy_config;
    }
    DynamicJsonDocument config = xtouch_filesystem_readJson(SPIFFS, configPath, false, XTOUCH_CONFIG_CAPACITY);
    xtouch_config_error = xtouch_config_normalize(config);
    if (xtouch_config_error == nullptr)
        xtouch_config_error = xtouch_config_validate(config.as<JsonObjectConst>());

    // Keep display settings intact. Never truncate credentials and try to use them.
    xTouchConfig.xTouchMqttCloud = false;
    xTouchConfig.xTouchMqttPort = 8883;
    xTouchConfig.xTouchHost[0] = '\0';
    xTouchConfig.xTouchAccessCode[0] = '\0';
    xTouchConfig.xTouchSerialNumber[0] = '\0';
    xTouchConfig.xTouchPrinterModel[0] = '\0';
    xTouchConfig.xTouchMqttRegion[0] = '\0';
    xTouchConfig.xTouchMqttUsername[0] = '\0';
    xTouchConfig.xTouchMqttAuthToken[0] = '\0';
    if (xtouch_config_error != nullptr)
    {
        ConsoleError.printf("[XTouch][CONFIG] %s\n", xtouch_config_error);
        return config;
    }

    JsonObjectConst mqtt = config["mqtt"];
    xTouchConfig.xTouchMqttCloud = strcmp(xtouch_config_string(mqtt, "mode"), "cloud") == 0;
    xTouchConfig.xTouchMqttPort = mqtt["port"].as<uint16_t>();
    xtouch_config_copy(xTouchConfig.xTouchHost, sizeof(xTouchConfig.xTouchHost), xtouch_config_string(mqtt, "host"));
    xtouch_config_copy(xTouchConfig.xTouchAccessCode, sizeof(xTouchConfig.xTouchAccessCode), xtouch_config_string(mqtt, "accessCode"));
    xtouch_config_copy(xTouchConfig.xTouchSerialNumber, sizeof(xTouchConfig.xTouchSerialNumber), xtouch_config_string(mqtt, "serialNumber"));
    xtouch_config_copy(xTouchConfig.xTouchPrinterModel, sizeof(xTouchConfig.xTouchPrinterModel),
                       xtouch_config_normalize_printer_model(xtouch_config_string(mqtt, "printerModel")));
    xtouch_config_copy(xTouchConfig.xTouchMqttRegion, sizeof(xTouchConfig.xTouchMqttRegion), xtouch_config_string(mqtt, "region"));
    xtouch_config_copy(xTouchConfig.xTouchMqttUsername, sizeof(xTouchConfig.xTouchMqttUsername), xtouch_config_string(mqtt, "username"));
    xtouch_config_copy(xTouchConfig.xTouchMqttAuthToken, sizeof(xTouchConfig.xTouchMqttAuthToken), xtouch_config_string(mqtt, "authToken"));
    return config;
}

#endif
