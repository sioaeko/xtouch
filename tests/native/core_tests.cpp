#include <cassert>
#include <iostream>
#include <string>
#include "xtouch/provisioning.h"
#include "xtouch/report_filter.h"
#include "xtouch/autogrowstream.h"

DynamicJsonDocument parse(const char *json)
{
    DynamicJsonDocument doc(XTOUCH_CONFIG_CAPACITY);
    assert(!deserializeJson(doc, json));
    return doc;
}

#include "control_tests.h"

int main()
{
    auto cloud = parse(R"({"ssid":" space wifi ","pwd":" secret ","timeout":7000,"mqtt":{"mode":"cloud","username":"u_123","authToken":"opaque-token","serialNumber":"01P00A123456789","printerModel":"P1S"}})");
    assert(xtouch_config_normalize(cloud) == nullptr);
    assert(xtouch_config_validate(cloud.as<JsonObjectConst>()) == nullptr);
    assert(cloud["mqtt"]["serialNumber"] == "01P00A123456789");
    assert(cloud["mqtt"]["authToken"] == "opaque-token");
    assert(cloud["ssid"] == " space wifi " && cloud["pwd"] == " secret ");
    assert(cloud["timeout"] == 7000);
    assert(cloud["mqtt"]["host"] == "us.mqtt.bambulab.com");
    assert(cloud["mqtt"]["port"] == 8883);
    assert(strcmp(xtouch_config_normalize_printer_model("P1S"), "C12") == 0);
    std::string once;
    serializeJson(cloud, once);
    assert(xtouch_config_normalize(cloud) == nullptr);
    std::string twice;
    serializeJson(cloud, twice);
    assert(once == twice); // Normalization must preserve already nested config.

    auto flat = parse(R"({"ssid":"wifi","pwd":"pass","cloud-region":"China","cloud-username":"u_123","cloud-authToken":"opaque-token","serial":"01P00A123456789","device_type":"P1S","host":"192.168.1.10","port":8883})");
    assert(xtouch_config_normalize(flat) == nullptr);
    assert(xtouch_config_validate(flat.as<JsonObjectConst>()) == nullptr);
    assert(flat["mqtt"]["mode"] == "cloud");
    assert(flat["mqtt"]["host"] == "cn.mqtt.bambulab.com");
    assert(flat["mqtt"]["port"] == 8883);
    assert(flat["mqtt"]["printerModel"] == "P1S");

    auto precedence = parse(R"({"authToken":"old","username":"u_old","mqtt":{"auth_token":"new","username":"u_new","mode":"local","local_mqtt":false,"host":"192.168.1.10","port":"18883"}})");
    assert(xtouch_config_normalize(precedence) == nullptr);
    assert(precedence["mqtt"]["authToken"] == "new");
    assert(precedence["mqtt"]["username"] == "u_new");
    assert(precedence["mqtt"]["mode"] == "local");
    assert(precedence["mqtt"]["host"] == "192.168.1.10");
    assert(precedence["mqtt"]["port"] == 18883);

    auto local = parse(R"({"ssid":"wifi","pwd":"","mqtt":{"host":"192.168.1.10","accessCode":"12345678","serialNumber":"01P00A123456789","printerModel":"C12"}})");
    assert(xtouch_config_normalize(local) == nullptr);
    assert(xtouch_config_validate(local.as<JsonObjectConst>()) == nullptr);
    assert(local["mqtt"]["mode"] == "local");
    local["mqtt"]["authToken"] = "stale-token";
    assert(xtouch_config_normalize(local) == nullptr && local["mqtt"]["mode"] == "local");
    for (const char *json : {"[]", "null", "{\"mqtt\":42}", "{\"mqtt\":{\"mode\":\"typo\"}}", "{\"local_mqtt\":\"false\"}", "{\"port\":70000}", "{\"port\":-1}", "{\"port\":0}", "{\"port\":1.5}", "{\"port\":\"8883oops\"}"})
    {
        auto invalid = parse(json);
        assert(xtouch_config_normalize(invalid) != nullptr);
    }
    auto badHost = parse(R"({"mode":"cloud","mqttHost":"us.mqtt.bambulab.com.evil.test"})");
    assert(xtouch_config_normalize(badHost) != nullptr);

    auto missing = cloud;
    missing["mqtt"].remove("serialNumber");
    assert(xtouch_config_validate(missing.as<JsonObjectConst>()) != nullptr);
    missing = cloud;
    missing["mqtt"]["serialNumber"] = "device/+/report";
    assert(xtouch_config_validate(missing.as<JsonObjectConst>()) != nullptr);
    missing = cloud;
    missing["mqtt"]["authToken"] = std::string(2048, 'x');
    assert(xtouch_config_validate(missing.as<JsonObjectConst>()) != nullptr);
    missing = cloud;
    missing["mqtt"]["authToken"] = 123;
    assert(xtouch_config_validate(missing.as<JsonObjectConst>()) != nullptr);
    missing = cloud;
    missing["mqtt"].remove("username");
    assert(xtouch_config_validate(missing.as<JsonObjectConst>()) != nullptr);
    missing["mqtt"]["authToken"] = "header.claims.signature";
    assert(xtouch_config_validate(missing.as<JsonObjectConst>()) == nullptr);
    char bounded[5];
    xtouch_config_copy(bounded, sizeof(bounded), "long-print-name");
    assert(strcmp(bounded, "long") == 0);
    xtouch_config_copy(bounded, sizeof(bounded), nullptr);
    assert(bounded[0] == '\0');
    std::cout << "PASS config: nested preservation, aliases, precedence, regions, P1S, validation\n";

    XtouchAutoGrowBufferStream stream;
    for (size_t i = 0; i < 32768; ++i) assert(stream.write('x') == 1);
    assert(!stream.failed() && stream.current_length() == 32768);
    assert(strlen(stream.get_buffer()) == 32768);
    assert(stream.write('x') == 0 && stream.failed());
    stream.flush();
    assert(!stream.failed() && stream.current_length() == 0 && !stream.includes("ams"));

    std::string report = R"({"print":{"command":"push_status","nozzle_temper":201,"gcode_state":"RUNNING","ams_status":0,"ams":{"tray_now":"0","ams":[{"id":"0","tray":[{"id":"0","tray_type":"PLA"}]}]}},"camera":{"command":"ipcam_record_set","control":"enable"}})";
    for (unsigned char c : report) assert(stream.write(c) == 1);
    StaticJsonDocument<256> filter;
    DynamicJsonDocument status(8192);
    xtouch_mqtt_report_filter(filter, false);
    assert(!deserializeJson(status, reinterpret_cast<const uint8_t *>(stream.get_buffer()), stream.current_length(), DeserializationOption::Filter(filter)));
    assert(status["print"]["nozzle_temper"] == 201);
    assert(!status["print"].containsKey("ams"));
    assert(status["camera"]["control"] == "enable");
    assert(stream.includes("\"ams\"") && report == stream.get_buffer());
    xtouch_mqtt_report_filter(filter, true);
    assert(!deserializeJson(status, reinterpret_cast<const uint8_t *>(stream.get_buffer()), stream.current_length(), DeserializationOption::Filter(filter)));
    assert(status["print"]["command"] == "push_status");
    assert(status["print"]["ams"]["tray_now"] == "0");
    assert(status["print"]["ams"]["ams"][0]["tray"][0]["tray_type"] == "PLA");
    assert(!status["print"].containsKey("nozzle_temper"));
    assert(report == stream.get_buffer());
    std::cout << "PASS reports: buffer boundaries, overflow recovery, status and AMS passes\n";
    test_control_protocol();
}
