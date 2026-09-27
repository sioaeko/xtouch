#ifndef _XLCD_MQTT
#define _XLCD_MQTT

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include <string.h>
#include <time.h>
#include "ui/ui_msgs.h"
#include "types.h"
#include "autogrowstream.h"
#include "report_filter.h"
#include "ams_report.h"
#include "light_report.h"
#include "bbl-certs.h"
// #include "xtouch/ams-status.hpp"

WiFiClientSecure xtouch_wiFiClientSecure;
PubSubClient xtouch_pubSubClient(xtouch_wiFiClientSecure);

String xtouch_mqtt_request_topic;
String xtouch_mqtt_report_topic;

#include "ams.h"
#include "device.h"
#include "config.h"

#define XTOUCH_MQTT_SERVER_TIMEOUT 20
#define XTOUCH_MQTT_SERVER_PUSH_STATUS_TIMEOUT 60
#define XTOUCH_MQTT_SERVER_JSON_PARSE_SIZE 8192

/* ---------------------------------------------- */
bool xtouch_mqtt_firstConnectionDone = false;
unsigned long xtouch_mqtt_lastPushStatus = 0;
bool xtouch_mqtt_needs_provisioning = false;
bool xtouch_mqtt_refresh_requested = false;
void xtouch_mqtt_onMqttReady();
uint32_t xtouch_mqtt_reconnect_delay = 1000;
unsigned long xtouch_mqtt_next_connect_at = 0;

XtouchAutoGrowBufferStream stream;

void xtouch_mqtt_sendMsg(XTOUCH_MESSAGE message, unsigned long long data = 0)
{
    XTOUCH_MESSAGE_DATA eventData;
    eventData.data = data;
    lv_msg_send(message, &eventData);
}

void xtouch_mqtt_topic_setup()
{
    String xtouch_device_topic = String("device/") + xTouchConfig.xTouchSerialNumber;
    xtouch_mqtt_request_topic = xtouch_device_topic + String("/request");
    xtouch_mqtt_report_topic = xtouch_device_topic + String("/report");
}

String xtouch_mqtt_parse_printer_type(String type_str)
{
    if (type_str == "3DPrinter-X1")
    {
        return "BL-P002";
    }
    else if (type_str == "3DPrinter-X1-Carbon")
    {
        return "BL-P001";
    }
    else if (type_str == "BL-P001")
    {
        return type_str;
    }
    else if (type_str == "BL-P003")
    {
        return type_str;
    }
    return "";
}

void xtouch_mqtt_update_slice_info(const char *project_id, const char *profile_id, const char *subtask_id, int plate_idx)
{
    xtouch_config_copy(bambuStatus.project_id_, sizeof(bambuStatus.project_id_), project_id);
    xtouch_config_copy(bambuStatus.profile_id_, sizeof(bambuStatus.profile_id_), profile_id);
    xtouch_config_copy(bambuStatus.subtask_id_, sizeof(bambuStatus.subtask_id_), subtask_id);
}

void xtouch_mqtt_processPushStatus(JsonDocument &incomingJson)
{
    xtouch_mqtt_lastPushStatus = millis();
    xtouch_mqtt_refresh_requested = false;
    bambuStatus.printer_status_received = true;
    xtouch_mqtt_onMqttReady();
    ConsoleDebug.println(F("[XTouch][MQTT] ProcessPushStatus"));

    if (incomingJson != NULL && incomingJson.containsKey("print"))
    {
        // #pragma region printing

        if (incomingJson["print"].containsKey("print_type"))
        {
            xtouch_config_copy(bambuStatus.print_type, sizeof(bambuStatus.print_type), incomingJson["print"]["print_type"]);
        }
        if (incomingJson["print"].containsKey("home_flag"))
        {
            bambuStatus.home_flag = incomingJson["print"]["home_flag"].as<int>();
        }

        if (incomingJson["print"].containsKey("hw_switch_state"))
        {
            bambuStatus.hw_switch_state = incomingJson["print"]["hw_switch_state"].as<int>();
        }

        if (incomingJson["print"].containsKey("mc_remaining_time"))
        {
            if (incomingJson["print"]["mc_remaining_time"].is<String>())
            {
                String timeStr = incomingJson["print"]["mc_remaining_time"].as<String>();

                bambuStatus.mc_left_time = atoi(timeStr.c_str()) * 60;
            }
            else if (incomingJson["print"]["mc_remaining_time"].is<int>())
            {
                bambuStatus.mc_left_time = incomingJson["print"]["mc_remaining_time"].as<int>() * 60;
            }
        }

        if (incomingJson["print"].containsKey("mc_percent"))
        {
            if (incomingJson["print"]["mc_percent"].is<String>())
                bambuStatus.mc_print_percent = atoi(incomingJson["print"]["mc_percent"].as<String>().c_str());
            else if (incomingJson["print"]["mc_percent"].is<int>())
                bambuStatus.mc_print_percent = incomingJson["print"]["mc_percent"].as<int>();
        }

        if (incomingJson["print"].containsKey("mc_print_sub_stage"))
        {
            bambuStatus.mc_print_sub_stage = incomingJson["print"]["mc_print_sub_stage"].as<int>();
        }

        if (incomingJson["print"].containsKey("mc_print_stage"))
        {
            if (incomingJson["print"]["mc_print_stage"].is<String>())
                bambuStatus.mc_print_stage = atoi(incomingJson["print"]["mc_print_stage"].as<String>().c_str());
            if (incomingJson["print"]["mc_print_stage"].is<int>())
                bambuStatus.mc_print_stage = incomingJson["print"]["mc_print_stage"].as<int>();
        }

        if (incomingJson["print"].containsKey("mc_print_error_code"))
        {
            if (incomingJson["print"]["mc_print_error_code"].is<int>())
                bambuStatus.mc_print_error_code = incomingJson["print"]["mc_print_error_code"].as<int>();
        }

        if (incomingJson["print"].containsKey("mc_print_line_number"))
        {
            if (incomingJson["print"]["mc_print_line_number"].is<String>())
            {
                String mc_print_line_number_str = incomingJson["print"]["mc_print_line_number"].as<String>();
                if (mc_print_line_number_str != "")
                {
                    bambuStatus.mc_print_line_number = atoi(mc_print_line_number_str.c_str());
                }
            }
        }

        if (incomingJson["print"].containsKey("print_error"))
        {
            if (incomingJson["print"]["print_error"].is<int>())
            {
                char prefix_str[9];
                bambuStatus.print_error = incomingJson["print"]["print_error"].as<int>();
                sprintf(prefix_str, "%08X", bambuStatus.print_error);

                if (xtouch_errors_isKeyPresent(prefix_str, device_error_keys, device_error_length))
                {
                    hms_enqueue(incomingJson["print"]["print_error"].as<unsigned long long>());
                    xtouch_mqtt_sendMsg(XTOUCH_ON_ERROR, 0);
                }
            }
        }

        // #pragma endregion

        // #pragma region online
        // #pragma endregion

        // #pragma region print_task
        if (incomingJson["print"].containsKey("printer_type"))
        {
            xtouch_config_copy(bambuStatus.printer_type, sizeof(bambuStatus.printer_type), xtouch_mqtt_parse_printer_type(incomingJson["print"]["printer_type"].as<String>()).c_str());
        }

        if (incomingJson["print"].containsKey("subtask_name"))
        {
            xtouch_config_copy(bambuStatus.subtask_name, sizeof(bambuStatus.subtask_name), incomingJson["print"]["subtask_name"]);
        }

        if (incomingJson["print"].containsKey("layer_num"))
        {
            bambuStatus.current_layer = incomingJson["print"]["layer_num"].as<int>();
        }

        if (incomingJson["print"].containsKey("total_layer_num"))
        {
            bambuStatus.total_layers = incomingJson["print"]["total_layer_num"].as<int>();
        }

        if (incomingJson["print"].containsKey("gcode_state"))
        {
            xtouch_device_set_print_state(incomingJson["print"]["gcode_state"].as<String>());
        }

        if (incomingJson["print"].containsKey("queue_number"))
        {
            bambuStatus.queue_number = incomingJson["print"]["queue_number"].as<int>();
        }

        if (incomingJson["print"].containsKey("task_id"))
        {
            xtouch_config_copy(bambuStatus.task_id, sizeof(bambuStatus.task_id), incomingJson["print"]["task_id"]);
        }

        if (incomingJson["print"].containsKey("gcode_file"))
        {
            xtouch_config_copy(bambuStatus.gcode_file, sizeof(bambuStatus.gcode_file), incomingJson["print"]["gcode_file"]);
        }

        if (incomingJson["print"].containsKey("gcode_file_prepare_percent"))
        {
            String percent_str = incomingJson["print"]["gcode_file_prepare_percent"].as<String>();
            if (percent_str != "")
            {
                bambuStatus.gcode_file_prepare_percent = atoi(percent_str.c_str());
            }
        }

        if (incomingJson["print"].containsKey("project_id") && incomingJson["print"].containsKey("profile_id") && incomingJson["print"].containsKey("subtask_id"))
        {
            String obj_subtask_id_string = incomingJson["print"]["subtask_id"].as<String>();
            xtouch_config_copy(bambuStatus.obj_subtask_id, sizeof(bambuStatus.obj_subtask_id), obj_subtask_id_string.c_str());

            int plate_index = -1;
            /* parse local plate_index from task */
            if (obj_subtask_id_string == "0" && incomingJson["print"]["profile_id"].as<String>() != "0")
            {
                if (incomingJson["print"].containsKey("gcode_file"))
                {
                    String gcode_file_string = incomingJson["print"]["gcode_file"].as<String>();
                    xtouch_config_copy(bambuStatus.gcode_file, sizeof(bambuStatus.gcode_file), incomingJson["print"]["gcode_file"]);

                    int idx_start = gcode_file_string.lastIndexOf("_") + 1;
                    int idx_end = gcode_file_string.lastIndexOf(".");
                    if (idx_start > 0 && idx_end > idx_start)
                    {
                        plate_index = atoi(gcode_file_string.substring(idx_start, idx_end).c_str());
                        bambuStatus.plate_index = plate_index;
                    }
                }
            }
            xtouch_mqtt_update_slice_info(incomingJson["print"]["project_id"], incomingJson["print"]["profile_id"], incomingJson["print"]["subtask_id"], plate_index);

            xtouch_config_copy(bambuStatus.task_id, sizeof(bambuStatus.task_id), incomingJson["print"]["subtask_id"]);
        }
        // #pragma region print_task

        // #pragma region status

        if (incomingJson["print"].containsKey("bed_temper"))
        {
            bambuStatus.bed_temper = incomingJson["print"]["bed_temper"].as<double>();
            xtouch_mqtt_sendMsg(XTOUCH_ON_BED_TEMP, bambuStatus.bed_temper);
        }

        if (incomingJson["print"].containsKey("bed_target_temper"))
        {
            bambuStatus.bed_target_temper = incomingJson["print"]["bed_target_temper"].as<double>();
            xtouch_mqtt_sendMsg(XTOUCH_ON_BED_TARGET_TEMP, bambuStatus.bed_target_temper);
        }

        if (incomingJson["print"].containsKey("frame_temper"))
        {
            bambuStatus.frame_temp = incomingJson["print"]["frame_temper"].as<double>();
        }

        if (incomingJson["print"].containsKey("nozzle_temper"))
        {
            bambuStatus.nozzle_temper = incomingJson["print"]["nozzle_temper"].as<double>();
            xtouch_mqtt_sendMsg(XTOUCH_ON_NOZZLE_TEMP, bambuStatus.nozzle_temper);
        }

        if (incomingJson["print"].containsKey("nozzle_target_temper"))
        {
            bambuStatus.nozzle_target_temper = incomingJson["print"]["nozzle_target_temper"].as<double>();
            xtouch_mqtt_sendMsg(XTOUCH_ON_NOZZLE_TARGET_TEMP, bambuStatus.nozzle_target_temper);
        }

        if (incomingJson["print"].containsKey("chamber_temper"))
        {
            bambuStatus.chamber_temper = incomingJson["print"]["chamber_temper"].as<double>();
            xtouch_mqtt_sendMsg(XTOUCH_ON_CHAMBER_TEMP, bambuStatus.chamber_temper);
        }

        // link_th
        // link_ams
        if (incomingJson["print"].containsKey("wifi_signal"))
        {
            String wifi_signal = incomingJson["print"]["wifi_signal"].as<String>();
            wifi_signal.replace("dBm", "");
            bambuStatus.wifi_signal = abs(wifi_signal.toInt());
            xtouch_mqtt_sendMsg(XTOUCH_ON_WIFI_SIGNAL, bambuStatus.wifi_signal);
        }

        if (incomingJson["print"].containsKey("fan_gear"))
        {
            uint32_t fan_gear = incomingJson["print"]["fan_gear"].as<uint32_t>();
            bambuStatus.cooling_fan_speed = (int)((fan_gear & 0x000000FF) >> 0);
            bambuStatus.big_fan1_speed = (int)((fan_gear & 0x0000FF00) >> 8);
            bambuStatus.big_fan2_speed = (int)((fan_gear & 0x00FF0000) >> 16);
            xtouch_mqtt_sendMsg(XTOUCH_ON_PART_FAN_SPEED, bambuStatus.cooling_fan_speed);
            xtouch_mqtt_sendMsg(XTOUCH_ON_PART_AUX_SPEED, bambuStatus.big_fan1_speed);
            xtouch_mqtt_sendMsg(XTOUCH_ON_PART_CHAMBER_SPEED, bambuStatus.big_fan2_speed);
        }
        else
        {
            if (incomingJson["print"].containsKey("cooling_fan_speed"))
            {
                int speed = incomingJson["print"]["cooling_fan_speed"].as<int>();
                bambuStatus.cooling_fan_speed = round(floor(speed / float(1.5)) * float(25.5));
                xtouch_mqtt_sendMsg(XTOUCH_ON_PART_FAN_SPEED, bambuStatus.cooling_fan_speed);
            }

            if (incomingJson["print"].containsKey("big_fan1_speed"))
            {
                int speed = incomingJson["print"]["big_fan1_speed"].as<int>();
                bambuStatus.big_fan1_speed = round(floor(speed / float(1.5)) * float(25.5));
                xtouch_mqtt_sendMsg(XTOUCH_ON_PART_AUX_SPEED, bambuStatus.big_fan1_speed);
            }

            if (incomingJson["print"].containsKey("big_fan2_speed"))
            {
                int speed = incomingJson["print"]["big_fan2_speed"].as<int>();
                bambuStatus.big_fan2_speed = round(floor(speed / float(1.5)) * float(25.5));
                xtouch_mqtt_sendMsg(XTOUCH_ON_PART_CHAMBER_SPEED, bambuStatus.big_fan2_speed);
            }
        }

        // heatbreak_fan_speed

        if (incomingJson["print"].containsKey("spd_lvl"))
        {
            bambuStatus.printing_speed_lvl = incomingJson["print"]["spd_lvl"].as<int>();
        }

        if (incomingJson["print"].containsKey("spd_mag"))
        {
            bambuStatus.printing_speed_mag = incomingJson["print"]["spd_mag"].as<int>();
        }

        // stg
        // stg_cur
        // filam_bak
        // mess_production_state
        // lifecycle

        if (incomingJson["print"].containsKey("lights_report"))
        {

            if (xtouch_light_report(incomingJson["print"]["lights_report"].as<JsonArrayConst>(), bambuStatus.chamberLed))
            {
                XTOUCH_MESSAGE_DATA eventData;
                eventData.data = bambuStatus.chamberLed ? 1 : 0;
                if (xtouch_light_last_published && bambuStatus.chamberLed == xtouch_light_last_target)
                    xtouch_light_result = "state_confirmed";
                lv_msg_send(XTOUCH_ON_LIGHT_REPORT, &eventData);
            }
        }

        // sdcard

        // #pragma endregion

        if (incomingJson["print"].containsKey("nozzle_diameter"))
        {
            if (incomingJson["print"]["nozzle_diameter"].is<float>())
            {
                bambuStatus.nozzle_diameter = incomingJson["print"]["nozzle_diameter"].as<float>();
            }
            else if (incomingJson["print"]["nozzle_diameter"].is<String>())
            {
                bambuStatus.nozzle_diameter = incomingJson["print"]["nozzle_diameter"].as<String>().toFloat();
            }
        }

        // #pragma region upgrade
        // #pragma endregion

        // #pragma region  camera
        if (incomingJson["print"].containsKey("ipcam"))
        {

            if (incomingJson["print"]["ipcam"].containsKey("ipcam_record"))
            {
                bambuStatus.camera_recording_when_printing = incomingJson["ipcam"]["ipcam_record"].as<String>() == "enable";
            }
            if (incomingJson["print"]["ipcam"].containsKey("timelapse"))
            {
                bambuStatus.camera_timelapse = incomingJson["print"]["ipcam"]["timelapse"].as<String>() == "enable";
            }
            if (incomingJson["print"]["ipcam"].containsKey("ipcam_dev"))
            {
                bambuStatus.has_ipcam = incomingJson["print"]["ipcam"]["ipcam_dev"].as<String>() == "1";
            }
            xtouch_mqtt_sendMsg(XTOUCH_ON_IPCAM);
        }

        // xcam

        // #pragma endregion

        // #pragma region hms
        if (incomingJson["print"].containsKey("hms"))
        {
            if (incomingJson["print"]["hms"].is<JsonArray>())
            {

                for (JsonVariant value : incomingJson["print"]["hms"].as<JsonArray>())
                {
                    JsonObject element = value.as<JsonObject>();
                    unsigned attr = element["attr"].as<unsigned>();
                    unsigned code = element["code"].as<unsigned>();
                    int module_id;
                    unsigned module_num;
                    unsigned part_id;
                    unsigned reserved;
                    int msg_level;
                    int msg_code;
                    unsigned int model_id_int = (attr >> 24) & 0xFF;
                    if (model_id_int < MODULE_MAX)
                        module_id = model_id_int;
                    else
                        module_id = MODULE_UKNOWN;
                    module_num = (attr >> 16) & 0xFF;
                    part_id = (attr >> 8) & 0xFF;
                    reserved = (attr >> 0) & 0xFF;
                    unsigned msg_level_int = code >> 16;
                    if (msg_level_int < HMS_MSG_LEVEL_MAX)
                        msg_level = msg_level_int;
                    else
                        msg_level = HMS_UNKNOWN;
                    msg_code = code & 0xFFFF;

                    char buffer[17];
                    sprintf(buffer, "%02X%02X%02X00000%01X%04X",
                            module_id,
                            module_num,
                            part_id,
                            msg_level,
                            msg_code);

                    char *endPtr;

                    unsigned long long intValue = strtoull(buffer, &endPtr, 16);

                    if (xtouch_errors_isKeyPresent(buffer, hms_error_values, hms_error_length))
                    {
                        hms_enqueue(intValue);
                        xtouch_mqtt_sendMsg(XTOUCH_ON_ERROR, 0);
                    }
                }
            }
        }

        // #pragma endregion

        // Update AMS metadata even when the report contains only tray_now or
        // ams_status. A full tray inventory is not required for these deltas.
        JsonObjectConst report = incomingJson["print"];
        xtouch_update_ams_report(bambuStatus, report);
        if (report.containsKey("ams"))
            xtouch_mqtt_sendMsg(XTOUCH_ON_AMS, bambuStatus.ams ? 1 : 0);
        if (report.containsKey("ams") || report.containsKey("ams_status") ||
            report.containsKey("hw_switch_state") || report.containsKey("gcode_state") ||
            report.containsKey("vt_tray"))
        {
            bambuStatus.is_ams_need_update = true;
            xtouch_mqtt_sendMsg(XTOUCH_ON_AMS_BITS, 0);
        }

        if (incomingJson["print"].containsKey("gcode_state") ||
            incomingJson["print"].containsKey("layer_num") ||
            incomingJson["print"].containsKey("total_layer_num") ||
            incomingJson["print"].containsKey("mc_remaining_time") ||
            incomingJson["print"].containsKey("mc_percent") ||
            incomingJson["print"].containsKey("spd_lvl") ||
            incomingJson["print"].containsKey("spd_mag"))
        {

            xtouch_mqtt_sendMsg(XTOUCH_ON_PRINT_STATUS);
        }
    }
}

void xtouch_mqtt_parseMessage(char *topic, const byte *payload, unsigned int length, byte type = 0)
{

    ConsoleDebug.println(F("[XTouch][MQTT] ParseMessage"));
    DynamicJsonDocument incomingJson(XTOUCH_MQTT_SERVER_JSON_PARSE_SIZE);

    StaticJsonDocument<256> amsFilter;
    xtouch_mqtt_report_filter(amsFilter, type != 0);

    auto deserializeError = deserializeJson(incomingJson, payload, length, DeserializationOption::Filter(amsFilter));

    // xtouch_debug_json(incomingJson);
    if (!deserializeError)
    {
        JsonObjectConst system = incomingJson["system"];
        if (system["command"] == "ledctrl" && system["sequence_id"].as<uint32_t>() == xtouch_light_sequence &&
            xtouch_light_requests != 0)
        {
            if (system["result"] == "fail" || system["result"] == "failed" || system["result"] == "error")
                xtouch_light_result = "rejected";
            else if (system["result"] == "success" && strcmp(xtouch_light_result, "state_confirmed") != 0)
                xtouch_light_result = "acknowledged";
            ConsoleInfo.printf("[XTouch][LIGHT] response=%s\n", xtouch_light_result);
        }
        if (incomingJson["info"]["command"] == "get_version")
        {
            for (JsonObjectConst module : incomingJson["info"]["module"].as<JsonArrayConst>())
            {
                if (module["name"] != "ota") continue;
                const char *version = module["sw_ver"] | "";
                xtouch_config_copy(bambuStatus.printer_firmware, sizeof(bambuStatus.printer_firmware), version);
                bambuStatus.native_filament_supported = xtouch_bblp_is_p1Series() &&
                                                       xtouch_p1_native_filament_supported(version);
                break;
            }
        }

        if (incomingJson.containsKey("print") && incomingJson["print"].containsKey("command"))
        {

            String command = incomingJson["print"]["command"].as<String>();

            if (command == "push_status")
            {
                xtouch_mqtt_processPushStatus(incomingJson);
            }
            else if (command == "gcode_line")
            {
                ConsoleDebug.println(F("[XTouch][MQTT] gcode_line ack"));
                ConsoleDebug.println(String((char *)payload));
            }

            // project_file
            // ams_filament_setting
            // xcam_control_set
            // print_option
            // extrusion_cali | flowrate_cali
            // extrusion_cali_set
            // extrusion_cali_sel
            // extrusion_cali_get
            // extrusion_cali_get_result
            // flowrate_get_result
        }

        // info

        if (incomingJson.containsKey("camera"))
        {
            if (incomingJson["camera"].containsKey("command"))
            {
                if (incomingJson["camera"]["command"].as<String>() == "ipcam_timelapse")
                {
                    bambuStatus.camera_timelapse = incomingJson["camera"]["control"].as<String>() == "enable";
                }
                else if (incomingJson["camera"]["command"].as<String>() == "ipcam_record_set")
                {
                    bambuStatus.camera_recording_when_printing = incomingJson["camera"]["control"].as<String>() == "enable";
                }
                xtouch_mqtt_sendMsg(XTOUCH_ON_IPCAM);
            }
        }

        // upgrade
        // event info
    }
    else
    {
        ConsoleError.println(F("[XTouch][MQTT] ParseMessage deserializeJson failed"));
    }

    // if (firstParseMessage)
    // {
    //     firstParseMessage = false;
    //     xtouch_device_command_getPaCalibration();
    // }
}

void xtouch_pubSubClient_streamCallback(char *topic, byte *payload, unsigned int length)
{
    if (stream.failed())
    {
        // ConsoleError expands to an if statement. Keep this branch scoped so
        // the else below belongs to the receive check, not the logging macro.
        ConsoleError.println(F("[XTouch][MQTT] Report exceeded the receive buffer; dropped"));
    }
    else if (strcmp(topic, xtouch_mqtt_report_topic.c_str()) == 0)
    {
        // const input prevents ArduinoJson zero-copy parsing from mutating the
        // buffer before the AMS pass and bounds all string searches.
        xtouch_mqtt_parseMessage(topic, reinterpret_cast<const byte *>(stream.get_buffer()), stream.current_length(), 0);
        if (stream.includes("\"ams\""))
            xtouch_mqtt_parseMessage(topic, reinterpret_cast<const byte *>(stream.get_buffer()), stream.current_length(), 1);
    }

    stream.flush();
}

String xtouch_mqtt_client_id()
{
    /*
     * Bambu's broker rejects duplicate MQTT client IDs. The old firmware
     * generated an ID but passed the literal string "clientId.c_str()" to
     * PubSubClient, so every XTouch appeared as the same client.
     */
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    return String("XTOUCH-") + mac;
}

bool xtouch_mqtt_derive_cloud_username()
{
    if (xTouchConfig.xTouchMqttUsername[0] != '\0')
        return true;

    const char *token = xTouchConfig.xTouchMqttAuthToken;
    const char *payloadStart = strchr(token, '.');
    if (payloadStart == nullptr)
        return false;

    payloadStart++;
    const char *payloadEnd = strchr(payloadStart, '.');
    if (payloadEnd == nullptr)
        return false;

    const size_t payloadLength = static_cast<size_t>(payloadEnd - payloadStart);
    if (payloadLength == 0 || payloadLength >= 768)
        return false;

    char encoded[768];
    memcpy(encoded, payloadStart, payloadLength);
    encoded[payloadLength] = '\0';

    /* JWT uses base64url while mbedTLS expects regular base64. */
    for (size_t i = 0; i < payloadLength; i++)
    {
        if (encoded[i] == '-')
            encoded[i] = '+';
        else if (encoded[i] == '_')
            encoded[i] = '/';
    }

    String encodedString(encoded);
    while ((encodedString.length() % 4) != 0)
        encodedString += '=';

    unsigned char decoded[768];
    size_t decodedLength = 0;
    const int decodeResult = mbedtls_base64_decode(
        decoded,
        sizeof(decoded) - 1,
        &decodedLength,
        reinterpret_cast<const unsigned char *>(encodedString.c_str()),
        encodedString.length());
    if (decodeResult != 0 || decodedLength == 0 || decodedLength >= sizeof(decoded))
        return false;

    decoded[decodedLength] = '\0';
    DynamicJsonDocument claims(1024);
    if (deserializeJson(claims, decoded, decodedLength) != DeserializationError::Ok)
        return false;

    const char *username = claims["username"] | "";
    if (username[0] == '\0' || strlen(username) >= sizeof(xTouchConfig.xTouchMqttUsername))
        return false;

    xtouch_config_copy(
        xTouchConfig.xTouchMqttUsername,
        sizeof(xTouchConfig.xTouchMqttUsername),
        username);
    return true;
}

void xtouch_mqtt_schedule_retry()
{
    xtouch_mqtt_next_connect_at = millis() + xtouch_mqtt_reconnect_delay;
    xtouch_mqtt_reconnect_delay = min<uint32_t>(xtouch_mqtt_reconnect_delay * 2, 30000);
}

void xtouch_mqtt_onMqttReady()
{
    if (!xtouch_mqtt_firstConnectionDone || xTouchConfig.currentScreenIndex == -1)
    {
        loadScreen(0);
        xtouch_screen_startScreenTimer();
        ConsoleInfo.println(F("[XTouch][MQTT] Printer push_status received"));
    }
    xtouch_mqtt_firstConnectionDone = true;
}

void xtouch_mqtt_show_provisioning(const char *reason)
{
    xtouch_mqtt_needs_provisioning = true;
    if (xTouchConfig.currentScreenIndex != -1) loadScreen(-1);
    if (xtouch_screen_onScreenOffTimer) lv_timer_pause(xtouch_screen_onScreenOffTimer);
    xtouch_screen_touchFromPowerOff = false;
    xtouch_screen_setBrightness(xTouchConfig.xTouchBacklightLevel);
    String message = String(reason) + "\nProvision at " + WiFi.localIP().toString();
    lv_label_set_text(introScreenCaption, message.c_str());
    lv_timer_handler();
    xtouch_mqtt_next_connect_at = millis() + 30000;
}

void xtouch_mqtt_connect()
{
    if (xtouch_pubSubClient.connected()) return;
    const unsigned long now = millis();
    if (xtouch_mqtt_next_connect_at != 0 && static_cast<int32_t>(now - xtouch_mqtt_next_connect_at) < 0) return;
    if (WiFi.status() != WL_CONNECTED)
    {
        xtouch_mqtt_schedule_retry();
        return;
    }
    if (xtouch_config_error != nullptr || xTouchConfig.xTouchHost[0] == '\0' || xTouchConfig.xTouchSerialNumber[0] == '\0')
    {
        xtouch_mqtt_show_provisioning("Check MQTT settings");
        return;
    }
    const char *username = "bblp";
    const char *password = xTouchConfig.xTouchAccessCode;
    if (xTouchConfig.xTouchMqttCloud)
    {
        if (!xtouch_mqtt_derive_cloud_username() || xTouchConfig.xTouchMqttAuthToken[0] == '\0')
        {
            xtouch_mqtt_show_provisioning("Cloud credentials missing");
            return;
        }
        // An ESP32 without an RTC starts in 1970. Let SNTP run while the UI
        // and provisioning endpoint remain responsive; never disable TLS.
        if (time(nullptr) < 1704067200)
        {
            if (xTouchConfig.currentScreenIndex == -1)
                lv_label_set_text(introScreenCaption, "Syncing clock for Cloud TLS");
            ConsoleInfo.println(F("[XTouch][MQTT] Waiting for network time (NTP)"));
            xtouch_mqtt_next_connect_at = millis() + 5000;
            return;
        }
        username = xTouchConfig.xTouchMqttUsername;
        password = xTouchConfig.xTouchMqttAuthToken;
    }
    if (xTouchConfig.currentScreenIndex == -1 && !xtouch_mqtt_needs_provisioning)
    {
        lv_label_set_text(introScreenCaption, xTouchConfig.xTouchMqttCloud ? "Connecting to Cloud MQTT" : "Connecting to printer");
        lv_timer_handler();
    }
    ConsoleInfo.printf("[XTouch][MQTT] Connecting to %s:%u\n", xTouchConfig.xTouchHost, xTouchConfig.xTouchMqttPort);
    stream.flush();
    String clientId = xtouch_mqtt_client_id();
    if (xtouch_pubSubClient.connect(clientId.c_str(), username, password))
    {
        if (!xtouch_pubSubClient.subscribe(xtouch_mqtt_report_topic.c_str()))
        {
            ConsoleError.println(F("[XTouch][MQTT] Failed to send subscription"));
            xtouch_pubSubClient.disconnect();
            xtouch_mqtt_schedule_retry();
            return;
        }
        ConsoleInfo.printf("[XTouch][MQTT] Connected; subscription sent to %s\n", xtouch_mqtt_report_topic.c_str());
        xtouch_mqtt_needs_provisioning = false;
        xtouch_mqtt_reconnect_delay = 1000;
        xtouch_mqtt_next_connect_at = 0;
        xtouch_mqtt_refresh_requested = false;
        xtouch_device_get_version();
        xtouch_device_pushall();
        // Broker authentication does not prove the printer is online.
        if (xTouchConfig.currentScreenIndex == -1)
            lv_label_set_text(introScreenCaption, "Cloud connected; waiting for printer");
        xtouch_mqtt_lastPushStatus = millis();
        return;
    }
    const int state = xtouch_pubSubClient.state();
    ConsoleError.printf("[XTouch][MQTT] Connection failed: %d\n", state);
    if (state == MQTT_CONNECT_BAD_CREDENTIALS || state == MQTT_CONNECT_UNAUTHORIZED)
    {
        xtouch_mqtt_show_provisioning("MQTT authentication failed");
        return;
    }
    if (xTouchConfig.currentScreenIndex == -1 && !xtouch_mqtt_needs_provisioning)
        lv_label_set_text(introScreenCaption, "MQTT retrying");
    // Schedule relative to completion, so a slow TLS failure still backs off.
    xtouch_mqtt_schedule_retry();
}

void xtouch_mqtt_setup()
{
    lv_label_set_text(introScreenCaption, LV_SYMBOL_CHARGE " Connecting Printer");
    lv_timer_handler();
    lv_task_handler();
    delay(32);

    xtouch_mqtt_topic_setup();

    xtouch_wiFiClientSecure.flush();
    xtouch_wiFiClientSecure.stop();

    if (xTouchConfig.xTouchMqttCloud)
    {
        xtouch_wiFiClientSecure.setCACert(us_mqtt_bambulab_com);
        configTime(0, 0, "time.cloudflare.com", "pool.ntp.org", "time.google.com");
    }
    else
    {
        // The existing local-printer path uses its self-signed certificate.
        xtouch_wiFiClientSecure.setInsecure();
    }
    xtouch_wiFiClientSecure.setTimeout(XTOUCH_MQTT_SERVER_TIMEOUT);
    xtouch_wiFiClientSecure.setHandshakeTimeout(8);
    xtouch_pubSubClient.setSocketTimeout(5);

    xtouch_pubSubClient.setServer(xTouchConfig.xTouchHost, xTouchConfig.xTouchMqttPort);
    xtouch_pubSubClient.setBufferSize(4096); // Cloud auth tokens can make CONNECT larger than 2KB.
    xtouch_pubSubClient.setStream(stream);
    xtouch_pubSubClient.setCallback(xtouch_pubSubClient_streamCallback);
    /* Match ha-bambulab's 30s keepalive; the broker allows 1.5x this window. */
    xtouch_pubSubClient.setKeepAlive(30);

    /* home */
    lv_msg_subscribe(XTOUCH_COMMAND_LIGHT_TOGGLE, xtouch_device_onLightToggleCommand, NULL);
    lv_msg_subscribe(XTOUCH_COMMAND_STOP, (lv_msg_subscribe_cb_t)xtouch_device_onStopCommand, NULL);
    lv_msg_subscribe(XTOUCH_COMMAND_PAUSE, (lv_msg_subscribe_cb_t)xtouch_device_onPauseCommand, NULL);
    lv_msg_subscribe(XTOUCH_COMMAND_RESUME, (lv_msg_subscribe_cb_t)xtouch_device_onResumeCommand, NULL);

    /* control */
    lv_msg_subscribe(XTOUCH_COMMAND_HOME, (lv_msg_subscribe_cb_t)xtouch_device_onHomeCommand, NULL);
    lv_msg_subscribe(XTOUCH_COMMAND_LEFT, (lv_msg_subscribe_cb_t)xtouch_device_onLeftCommand, NULL);
    lv_msg_subscribe(XTOUCH_COMMAND_RIGHT, (lv_msg_subscribe_cb_t)xtouch_device_onRightCommand, NULL);
    lv_msg_subscribe(XTOUCH_COMMAND_UP, (lv_msg_subscribe_cb_t)xtouch_device_onUpCommand, NULL);
    lv_msg_subscribe(XTOUCH_COMMAND_DOWN, (lv_msg_subscribe_cb_t)xtouch_device_onDownCommand, NULL);
    lv_msg_subscribe(XTOUCH_COMMAND_BED_TARGET_TEMP, (lv_msg_subscribe_cb_t)xtouch_device_onBedTargetTempCommand, NULL);
    lv_msg_subscribe(XTOUCH_COMMAND_NOZZLE_TARGET_TEMP, (lv_msg_subscribe_cb_t)xtouch_device_onNozzleTargetCommand, NULL);
    lv_msg_subscribe(XTOUCH_COMMAND_PART_FAN_SPEED, (lv_msg_subscribe_cb_t)xtouch_device_onPartSpeedCommand, NULL);
    lv_msg_subscribe(XTOUCH_COMMAND_AUX_FAN_SPEED, (lv_msg_subscribe_cb_t)xtouch_device_onAuxSpeedCommand, NULL);
    lv_msg_subscribe(XTOUCH_COMMAND_CHAMBER_FAN_SPEED, (lv_msg_subscribe_cb_t)xtouch_device_onChamberSpeedCommand, NULL);
    lv_msg_subscribe(XTOUCH_COMMAND_PRINT_SPEED, (lv_msg_subscribe_cb_t)xtouch_device_onPrintSpeedCommand, NULL);
    lv_msg_subscribe(XTOUCH_COMMAND_UNLOAD_FILAMENT, (lv_msg_subscribe_cb_t)xtouch_device_onUnloadFilament, NULL);
    lv_msg_subscribe(XTOUCH_COMMAND_LOAD_FILAMENT, (lv_msg_subscribe_cb_t)xtouch_device_onLoadFilament, NULL);
    lv_msg_subscribe(XTOUCH_COMMAND_AMS_CONTROL, (lv_msg_subscribe_cb_t)xtouch_device_command_ams_control, NULL);
    lv_msg_subscribe(XTOUCH_COMMAND_CLEAN_PRINT_ERROR, (lv_msg_subscribe_cb_t)xtouch_device_command_clean_print_error, NULL);

    /* filament */
    lv_msg_subscribe(XTOUCH_COMMAND_EXTRUDE_UP, (lv_msg_subscribe_cb_t)xtouch_device_onNozzleUp, NULL);
    lv_msg_subscribe(XTOUCH_COMMAND_EXTRUDE_DOWN, (lv_msg_subscribe_cb_t)xtouch_device_onNozzleDown, NULL);

    delay(2000);
}

void xtouch_mqtt_loop()
{
    xtouch_pubSubClient.loop();
    if (!xtouch_pubSubClient.connected())
    {
        xtouch_mqtt_connect();
        return;
    }

    const unsigned long quietFor = millis() - xtouch_mqtt_lastPushStatus;
    if (quietFor > XTOUCH_MQTT_SERVER_PUSH_STATUS_TIMEOUT * 1000UL && !xtouch_mqtt_refresh_requested)
    {
        ConsoleInfo.println(F("[XTouch][MQTT] No status for 60s; requesting status push"));
        xtouch_device_start_push();
        xtouch_device_pushall();
        xtouch_mqtt_refresh_requested = true;
    }
    if (quietFor > XTOUCH_MQTT_SERVER_PUSH_STATUS_TIMEOUT * 2000UL)
    {
        ConsoleError.println(F("[XTouch][MQTT] Printer still silent; reconnecting"));
        xtouch_pubSubClient.disconnect();
        stream.flush();
        xtouch_mqtt_schedule_retry();
        return;
    }

    delay(10);
}

#endif
