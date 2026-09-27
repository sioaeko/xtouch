#ifndef XTOUCH_COMMANDS_H
#define XTOUCH_COMMANDS_H

#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>

// P1 command envelopes follow ha-bambulab; see LICENSE-3RD-PARTY.md.
inline JsonObject xtouch_command_begin(JsonDocument &doc, const char *section,
                                      const char *command, const char *sequence)
{
    doc.clear();
    JsonObject body = doc.createNestedObject(section);
    body["sequence_id"] = sequence;
    body["command"] = command;
    return body;
}

inline void xtouch_command_print_action(JsonDocument &doc, const char *action, const char *sequence)
{
    xtouch_command_begin(doc, "print", action, sequence);
}

inline void xtouch_command_gcode(JsonDocument &doc, const char *line, const char *sequence)
{
    xtouch_command_begin(doc, "print", "gcode_line", sequence)["param"] = line;
}

inline void xtouch_command_light(JsonDocument &doc, bool on, const char *sequence)
{
    JsonObject body = xtouch_command_begin(doc, "system", "ledctrl", sequence);
    body["led_node"] = "chamber_light";
    body["led_mode"] = on ? "on" : "off";
    body["led_on_time"] = 500;
    body["led_off_time"] = 500;
    body["loop_times"] = 0;
    body["interval_time"] = 0;
}

inline bool xtouch_command_filament(JsonDocument &doc, int target, int activeTray,
                                   int temperature, const char *sequence)
{
    doc.clear();
    if (!((target >= 0 && target <= 15) || target == 254 || target == 255) ||
        temperature < 0 || temperature > 300) return false;
    const bool unload = target == 255;
    const int tray = unload ? activeTray : target;
    JsonObject body = xtouch_command_begin(doc, "print", "ams_change_filament", sequence);
    body["ams_id"] = tray >= 0 && tray <= 15 ? tray / 4 : 255;
    body["slot_id"] = unload ? 255 : (target == 254 ? 0 : target % 4);
    body["target"] = target;
    body["curr_temp"] = 0;
    body["tar_temp"] = unload ? 0 : temperature;
    return !doc.overflowed();
}

inline bool xtouch_p1_native_filament_supported(const char *version)
{
    unsigned parts[4] = {};
    char extra;
    if (version == nullptr || sscanf(version, "%u.%u.%u.%u%c", &parts[0], &parts[1],
                                    &parts[2], &parts[3], &extra) != 4) return false;
    const unsigned minimum[] = {1, 2, 99, 10};
    for (int i = 0; i < 4; ++i)
    {
        if (parts[i] != minimum[i]) return parts[i] > minimum[i];
    }
    return true;
}

#endif
