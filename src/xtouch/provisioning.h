#ifndef XTOUCH_PROVISIONING_H
#define XTOUCH_PROVISIONING_H

#include <ArduinoJson.h>
#include <cstring>
#include <cstdlib>
#include <initializer_list>

// Shared by internal loading, SD import, HTTP and USB provisioning.
constexpr size_t XTOUCH_CONFIG_CAPACITY = 8192;

inline const char *xtouch_config_string(JsonObjectConst object, const char *key)
{
    return object[key].is<const char *>() ? object[key].as<const char *>() : "";
}

inline void xtouch_config_copy(char *destination, size_t size, const char *source)
{
    if (size == 0 || destination == nullptr)
        return;
    strncpy(destination, source == nullptr ? "" : source, size - 1);
    destination[size - 1] = '\0';
}

inline const char *xtouch_config_normalize_printer_model(const char *model)
{
    if (strcmp(model, "P1P") == 0) return "C11";
    if (strcmp(model, "P1S") == 0) return "C12";
    if (strcmp(model, "X1") == 0) return "3DPrinter-X1";
    if (strcmp(model, "X1C") == 0 || strcmp(model, "X1 Carbon") == 0 ||
        strcmp(model, "X1-Carbon") == 0) return "3DPrinter-X1-Carbon";
    return model;
}

inline bool xtouch_config_cloud_host(const char *host)
{
    return strcmp(host, "us.mqtt.bambulab.com") == 0 ||
           strcmp(host, "cn.mqtt.bambulab.com") == 0;
}

inline void xtouch_config_alias(JsonObject mqtt, JsonObjectConst root,
                                const char *key, std::initializer_list<const char *> aliases)
{
    // A nested field always takes precedence, even over a flat canonical key.
    if (mqtt.containsKey(key)) return;
    for (const char *alias : aliases)
        if (mqtt.containsKey(alias)) { mqtt[key].set(mqtt[alias]); return; }
    if (root.containsKey(key)) { mqtt[key].set(root[key]); return; }
    for (const char *alias : aliases)
        if (root.containsKey(alias)) { mqtt[key].set(root[alias]); return; }
}

inline const char *xtouch_config_normalize(DynamicJsonDocument &config)
{
    if (!config.is<JsonObject>()) return "configuration must be an object";
    JsonObject root = config.as<JsonObject>();
    if (root.containsKey("mqtt") && !root["mqtt"].is<JsonObject>())
        return "mqtt must be an object";
    JsonObject mqtt = root["mqtt"].as<JsonObject>();
    // to<JsonObject>() clears an existing object. Only create when absent.
    if (mqtt.isNull()) mqtt = root.createNestedObject("mqtt");

    xtouch_config_alias(mqtt, root, "mode", {});
    xtouch_config_alias(mqtt, root, "local_mqtt", {});
    xtouch_config_alias(mqtt, root, "cloud", {});
    xtouch_config_alias(mqtt, root, "host", {});
    xtouch_config_alias(mqtt, root, "mqttHost", {"cloudHost", "cloud-host"});
    xtouch_config_alias(mqtt, root, "port", {"mqttPort"});
    xtouch_config_alias(mqtt, root, "accessCode", {"access_code"});
    xtouch_config_alias(mqtt, root, "serialNumber", {"serial", "serial_number"});
    xtouch_config_alias(mqtt, root, "printerModel", {"device_type", "model"});
    xtouch_config_alias(mqtt, root, "region", {"cloud-region"});
    xtouch_config_alias(mqtt, root, "username", {"mqttUsername", "user_id", "userId", "cloud-username"});
    xtouch_config_alias(mqtt, root, "authToken", {"token", "auth_token", "password", "cloud-authToken", "cloud_authToken"});

    const char *mode = xtouch_config_string(mqtt, "mode");
    if (mqtt.containsKey("mode"))
    {
        if (strcmp(mode, "local") != 0 && strcmp(mode, "cloud") != 0 && strcmp(mode, "bambu_cloud") != 0)
            return "mode must be local or cloud";
    }
    else if (mqtt.containsKey("local_mqtt"))
    {
        if (!mqtt["local_mqtt"].is<bool>()) return "local_mqtt must be boolean";
        mode = mqtt["local_mqtt"].as<bool>() ? "local" : "cloud";
    }
    else if (mqtt.containsKey("cloud"))
    {
        if (!mqtt["cloud"].is<bool>()) return "cloud must be boolean";
        mode = mqtt["cloud"].as<bool>() ? "cloud" : "local";
    }
    else
        mode = (mqtt.containsKey("authToken") || mqtt.containsKey("username")) ? "cloud" : "local";
    const bool cloud = strcmp(mode, "local") != 0;
    mqtt["mode"] = cloud ? "cloud" : "local";

    if (!mqtt.containsKey("port")) mqtt["port"] = 8883;
    if (mqtt["port"].is<const char *>())
    {
        const char *port = mqtt["port"];
        char *end = nullptr;
        long value = strtol(port, &end, 10);
        if (*port == '\0' || *end != '\0' || value < 1 || value > 65535) return "invalid MQTT port";
        mqtt["port"] = value;
    }
    if (!mqtt["port"].is<unsigned int>() || mqtt["port"].as<unsigned int>() < 1 ||
        mqtt["port"].as<unsigned int>() > 65535) return "invalid MQTT port";

    if (cloud)
    {
        const char *region = xtouch_config_string(mqtt, "region");
        bool china = strcmp(region, "China") == 0 || strcmp(region, "china") == 0 ||
                     strcmp(region, "CN") == 0 || strcmp(region, "cn") == 0;
        mqtt["region"] = china ? "China" : "Global";
        const char *overrideHost = xtouch_config_string(mqtt, "mqttHost");
        const char *host = xtouch_config_string(mqtt, "host");
        if (mqtt.containsKey("mqttHost"))
        {
            if (!xtouch_config_cloud_host(overrideHost)) return "unsupported Cloud MQTT host";
            mqtt["host"] = overrideHost;
        }
        else if (!xtouch_config_cloud_host(host))
            // HA stores the printer LAN address in host even in Cloud mode.
            mqtt["host"] = china ? "cn.mqtt.bambulab.com" : "us.mqtt.bambulab.com";
    }

    if (config.overflowed()) return "configuration is too large";
    return nullptr;
}

inline const char *xtouch_config_validate(JsonObjectConst root)
{
    const char *ssid = xtouch_config_string(root, "ssid");
    if (strlen(ssid) == 0 || strlen(ssid) > 32) return "SSID must contain 1-32 bytes";
    if (!root["pwd"].is<const char *>() || strlen(root["pwd"].as<const char *>()) > 64)
        return "invalid WiFi password";
    JsonObjectConst mqtt = root["mqtt"];
    struct Field { const char *key; size_t limit; };
    const Field fields[] = {{"host", 63}, {"accessCode", 8}, {"serialNumber", 15},
                           {"printerModel", 31}, {"username", 63}, {"authToken", 2047}};
    for (const auto &field : fields)
        if (mqtt.containsKey(field.key) && (!mqtt[field.key].is<const char *>() ||
            strlen(mqtt[field.key].as<const char *>()) > field.limit))
            return "invalid type or length in MQTT settings";
    const char *serial = xtouch_config_string(mqtt, "serialNumber");
    if (*serial == '\0') return "printer serial is required";
    for (const char *p = serial; *p; ++p)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')))
            return "invalid printer serial";
    const char *model = xtouch_config_normalize_printer_model(xtouch_config_string(mqtt, "printerModel"));
    if (strcmp(model, "C11") != 0 && strcmp(model, "C12") != 0 &&
        strcmp(model, "3DPrinter-X1") != 0 && strcmp(model, "3DPrinter-X1-Carbon") != 0)
        return "supported models: P1P, P1S, X1, X1C";
    if (strcmp(xtouch_config_string(mqtt, "mode"), "cloud") == 0)
    {
        const char *token = xtouch_config_string(mqtt, "authToken");
        if (*token == '\0') return "Cloud authToken is required";
        if (*xtouch_config_string(mqtt, "username") == '\0' && strchr(token, '.') == nullptr)
            return "Cloud username is required for a non-JWT token";
    }
    else if (*xtouch_config_string(mqtt, "host") == '\0' ||
             strlen(xtouch_config_string(mqtt, "accessCode")) != 8)
        return "local host and 8-character access code are required";
    return nullptr;
}

#endif
