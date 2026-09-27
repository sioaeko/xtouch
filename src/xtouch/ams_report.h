#ifndef XTOUCH_AMS_REPORT_H
#define XTOUCH_AMS_REPORT_H

#include <ArduinoJson.h>
#include "filament_state.h"

// Cloud status reports are partial updates. AMS fields may arrive without
// either the outer ams object or its nested tray inventory array.
inline void xtouch_update_ams_report(XTouchBambuStatus &status, JsonObjectConst report)
{
    if (report.containsKey("ams_status"))
    {
        const int value = report["ams_status"].as<int>();
        status.ams_status_sub = value & 0xff;
        const int main = (value >> 8) & 0xff;
        switch (main)
        {
        case AMS_STATUS_MAIN_IDLE:
        case AMS_STATUS_MAIN_FILAMENT_CHANGE:
        case AMS_STATUS_MAIN_RFID_IDENTIFYING:
        case AMS_STATUS_MAIN_ASSIST:
        case AMS_STATUS_MAIN_CALIBRATION:
        case AMS_STATUS_MAIN_SELF_CHECK:
        case AMS_STATUS_MAIN_DEBUG:
            status.ams_status_main = main;
            break;
        default:
            status.ams_status_main = AMS_STATUS_MAIN_UNKNOWN;
        }
    }

    JsonObjectConst ams = report["ams"];
    if (!ams.isNull())
    {
        struct Bits { const char *key; long *value; };
        const Bits fields[] = {
            {"ams_exist_bits", &status.ams_exist_bits}, {"tray_exist_bits", &status.tray_exist_bits},
            {"tray_read_done_bits", &status.tray_read_done_bits}, {"tray_reading_bits", &status.tray_reading_bits},
            {"tray_is_bbl_bits", &status.tray_is_bbl_bits}, {"version", &status.ams_version}
        };
        for (const auto &field : fields)
            if (ams.containsKey(field.key)) *field.value = ams[field.key].as<long>();
        if (ams.containsKey("ams_exist_bits")) status.ams = status.ams_exist_bits != 0;
        else if (ams["ams"].is<JsonArrayConst>()) status.ams = ams["ams"].size() != 0;
        if (ams.containsKey("tray_reading_bits")) status.ams_support_use_ams = true;
        if (ams.containsKey("tray_now")) xtouch_filament_set_tray(&status, ams["tray_now"].as<int>());
        if (ams.containsKey("tray_tar")) status.m_tray_tar = ams["tray_tar"].as<int>();
        if (ams.containsKey("ams_rfid_status")) status.ams_rfid_status = ams["ams_rfid_status"].as<int>();
        if (ams.containsKey("humidity")) status.ams_humidity = ams["humidity"].as<int>();
        if (ams.containsKey("insert_flag") || ams.containsKey("power_on_flag") || ams.containsKey("calibrate_remain_flag"))
        {
            if (status.ams_user_setting_hold_count > 0) --status.ams_user_setting_hold_count;
            else
            {
                if (ams.containsKey("insert_flag")) status.ams_insert_flag = ams["insert_flag"].as<bool>();
                if (ams.containsKey("power_on_flag")) status.ams_power_on_flag = ams["power_on_flag"].as<bool>();
                if (ams.containsKey("calibrate_remain_flag")) status.ams_calibrate_remain_flag = ams["calibrate_remain_flag"].as<bool>();
            }
        }
    }
    if (report.containsKey("ams_rfid_status")) status.ams_rfid_status = report["ams_rfid_status"].as<int>();

    JsonObjectConst external = report["vt_tray"];
    if (!external.isNull())
    {
        status.ams_support_virtual_tray = true;
        if (external.containsKey("nozzle_temp_min")) status.external_nozzle_temp_min = external["nozzle_temp_min"].as<int>();
        if (external.containsKey("nozzle_temp_max")) status.external_nozzle_temp_max = external["nozzle_temp_max"].as<int>();
    }
}

#endif
