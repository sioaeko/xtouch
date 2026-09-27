#ifndef XTOUCH_REPORT_FILTER_H
#define XTOUCH_REPORT_FILTER_H
#include <ArduinoJson.h>

inline void xtouch_mqtt_report_filter(JsonDocument &filter, bool amsOnly)
{
    filter.clear();
    if (!amsOnly)
    {
        filter["print"]["*"] = true;
        filter["print"]["ams"] = false;
        filter["camera"] = true;
        filter["info"] = true;
        filter["system"] = true;
    }
    else
    {
        filter["print"]["command"] = true;
        filter["print"]["ams"] = true;
        filter["print"]["ams_status"] = true;
    }
}
#endif
