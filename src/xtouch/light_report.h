#ifndef XTOUCH_LIGHT_REPORT_H
#define XTOUCH_LIGHT_REPORT_H
#include <ArduinoJson.h>

// The array order is not part of the protocol. Omitted nodes are deltas,
// not an instruction to turn the chamber light off.
inline bool xtouch_light_report(JsonArrayConst lights, bool &on)
{
    for (JsonObjectConst light : lights)
    {
        if (light["node"] != "chamber_light") continue;
        if (light["mode"] == "on") { on = true; return true; }
        if (light["mode"] == "off") { on = false; return true; }
    }
    return false;
}
#endif
