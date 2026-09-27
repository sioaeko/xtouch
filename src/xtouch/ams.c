#include <Arduino.h>
#include "types.h"
#include "filament_state.h"

void xtouch_ams_parse_tray_now(const char *tray_now)
{
    if (tray_now == NULL || strlen(tray_now) == 0)
    {
        return;
    }
    else
    {
        char *end;
        const long tray = strtol(tray_now, &end, 10);
        if (*end == '\0' && tray >= 0 && tray <= 255)
            xtouch_filament_set_tray(&bambuStatus, (int)tray);
    }
}

void xtouch_ams_parse_status(int ams_status)
{
    bambuStatus.ams_status_sub = ams_status & 0xFF;
    int ams_status_main_int = (ams_status & 0xFF00) >> 8;
    if (ams_status_main_int == AMS_STATUS_MAIN_IDLE)
    {
        bambuStatus.ams_status_main = AMS_STATUS_MAIN_IDLE;
    }
    else if (ams_status_main_int == AMS_STATUS_MAIN_FILAMENT_CHANGE)
    {
        bambuStatus.ams_status_main = AMS_STATUS_MAIN_FILAMENT_CHANGE;
    }
    else if (ams_status_main_int == AMS_STATUS_MAIN_RFID_IDENTIFYING)
    {
        bambuStatus.ams_status_main = AMS_STATUS_MAIN_RFID_IDENTIFYING;
    }
    else if (ams_status_main_int == AMS_STATUS_MAIN_ASSIST)
    {
        bambuStatus.ams_status_main = AMS_STATUS_MAIN_ASSIST;
    }
    else if (ams_status_main_int == AMS_STATUS_MAIN_CALIBRATION)
    {
        bambuStatus.ams_status_main = AMS_STATUS_MAIN_CALIBRATION;
    }
    else if (ams_status_main_int == AMS_STATUS_MAIN_SELF_CHECK)
    {
        bambuStatus.ams_status_main = AMS_STATUS_MAIN_SELF_CHECK;
    }
    else if (ams_status_main_int == AMS_STATUS_MAIN_DEBUG)
    {
        bambuStatus.ams_status_main = AMS_STATUS_MAIN_DEBUG;
    }
    else
    {
        bambuStatus.ams_status_main = AMS_STATUS_MAIN_UNKNOWN;
    }
}

bool xtouch_has_ams() { return bambuStatus.ams_exist_bits != 0; }

bool xtouch_can_load_filament()
{
    return xtouch_filament_can_load(&bambuStatus);
}

bool xtouch_can_unload_filament()
{
    return xtouch_filament_can_unload(&bambuStatus);
}
