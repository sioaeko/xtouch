#pragma once
#include "xtouch/commands.h"
#include "xtouch/ams_report.h"
#include "xtouch/light_report.h"

static void expect_command(const JsonDocument &actual, const char *expectedJson)
{
    DynamicJsonDocument expected(2048);
    assert(!deserializeJson(expected, expectedJson));
    assert(actual.size() == expected.size());
    for (JsonPairConst section : expected.as<JsonObjectConst>())
    {
        JsonObjectConst actualBody = actual[section.key()];
        JsonObjectConst expectedBody = section.value();
        assert(actualBody.size() == expectedBody.size());
        for (JsonPairConst field : expectedBody)
            assert(actualBody[field.key()] == field.value());
    }
}

static void test_control_protocol()
{
    bool lightOn = true;
    auto lights = parse(R"({"lights_report":[{"node":"work_light","mode":"off"},{"node":"chamber_light","mode":"on"}]})");
    assert(xtouch_light_report(lights["lights_report"].as<JsonArrayConst>(), lightOn) && lightOn);
    lights = parse(R"({"lights_report":[{"node":"work_light","mode":"off"}]})");
    assert(!xtouch_light_report(lights["lights_report"].as<JsonArrayConst>(), lightOn) && lightOn);
    lights = parse(R"({"lights_report":[{"node":"chamber_light","mode":"off"}]})");
    assert(xtouch_light_report(lights["lights_report"].as<JsonArrayConst>(), lightOn) && !lightOn);
    lights = parse(R"({"lights_report":[{"node":"chamber_light","mode":"unknown"}]})");
    assert(!xtouch_light_report(lights["lights_report"].as<JsonArrayConst>(), lightOn) && !lightOn);
    StaticJsonDocument<256> filter;
    xtouch_mqtt_report_filter(filter, false);
    DynamicJsonDocument reply(1024);
    assert(!deserializeJson(reply, R"({"system":{"command":"ledctrl","sequence_id":"3","result":"fail"}})", DeserializationOption::Filter(filter)));
    assert(reply["system"]["result"] == "fail");
    // Expected envelopes are independent examples from ha-bambulab commands.py
    // and coordinator.py at the reference commit recorded in docs/cloud-mqtt.md.
    DynamicJsonDocument command(1024);
    xtouch_command_print_action(command, "pause", "42");
    expect_command(command, R"({"print":{"sequence_id":"42","command":"pause"}})");
    xtouch_command_print_action(command, "resume", "43");
    expect_command(command, R"({"print":{"sequence_id":"43","command":"resume"}})");
    xtouch_command_print_action(command, "stop", "44");
    expect_command(command, R"({"print":{"sequence_id":"44","command":"stop"}})");
    xtouch_command_light(command, true, "45");
    expect_command(command, R"({"system":{"sequence_id":"45","command":"ledctrl","led_node":"chamber_light","led_mode":"on","led_on_time":500,"led_off_time":500,"loop_times":0,"interval_time":0}})");
    xtouch_command_light(command, false, "46");
    expect_command(command, R"({"system":{"sequence_id":"46","command":"ledctrl","led_node":"chamber_light","led_mode":"off","led_on_time":500,"led_off_time":500,"loop_times":0,"interval_time":0}})");
    xtouch_command_gcode(command, "M104 S200\n", "47");
    expect_command(command, R"({"print":{"sequence_id":"47","command":"gcode_line","param":"M104 S200\n"}})");

    assert(xtouch_command_filament(command, 254, 255, 225, "48"));
    expect_command(command, R"({"print":{"sequence_id":"48","command":"ams_change_filament","ams_id":255,"slot_id":0,"target":254,"curr_temp":0,"tar_temp":225}})");
    assert(xtouch_command_filament(command, 5, 255, 220, "49"));
    expect_command(command, R"({"print":{"sequence_id":"49","command":"ams_change_filament","ams_id":1,"slot_id":1,"target":5,"curr_temp":0,"tar_temp":220}})");
    assert(xtouch_command_filament(command, 255, 6, 0, "50"));
    expect_command(command, R"({"print":{"sequence_id":"50","command":"ams_change_filament","ams_id":1,"slot_id":255,"target":255,"curr_temp":0,"tar_temp":0}})");
    assert(xtouch_command_filament(command, 255, 254, 0, "51"));
    expect_command(command, R"({"print":{"sequence_id":"51","command":"ams_change_filament","ams_id":255,"slot_id":255,"target":255,"curr_temp":0,"tar_temp":0}})");
    assert(xtouch_command_filament(command, 254, 255, 0, "52"));
    assert(command["print"]["tar_temp"] == 0); // Unconfigured material delegates temperature choice to the printer, as HA does.
    assert(!xtouch_command_filament(command, 16, 255, 0, "53") && command.size() == 0);
    assert(!xtouch_command_filament(command, 254, 255, 301, "54") && command.size() == 0);
    assert(!xtouch_p1_native_filament_supported("01.02.99.09"));
    assert(xtouch_p1_native_filament_supported("01.02.99.10"));
    assert(xtouch_p1_native_filament_supported("01.09.00.00"));
    assert(!xtouch_p1_native_filament_supported("unknown"));
    assert(!xtouch_p1_native_filament_supported("01.09.00.00junk"));

    XTouchBambuStatus state = {};
    state.m_tray_now = 255;
    state.ams_status_main = AMS_STATUS_MAIN_UNKNOWN;
    assert(!xtouch_filament_can_load(&state) && !xtouch_filament_can_unload(&state));
    state.printer_status_received = true;
    state.print_status = XTOUCH_PRINT_STATUS_FINISHED;
    auto full = parse(R"({"ams_status":0,"ams":{"ams_exist_bits":"3","tray_exist_bits":"33","tray_now":"5","tray_tar":"5","ams":[{"id":"0"},{"id":"1"}]},"vt_tray":{"nozzle_temp_min":"210","nozzle_temp_max":"230"}})");
    xtouch_update_ams_report(state, full.as<JsonObjectConst>());
    assert(state.m_tray_now == 5 && state.m_ams_id == 1 && state.m_tray_id == 1);
    assert(state.ams && state.ams_exist_bits == 3 && state.tray_exist_bits == 33);
    assert(!xtouch_filament_can_load(&state) && xtouch_filament_can_unload(&state));
    assert(state.external_nozzle_temp_min == 210 && state.external_nozzle_temp_max == 230);

    auto moving = parse(R"({"ams_status":258})");
    xtouch_update_ams_report(state, moving.as<JsonObjectConst>());
    assert(state.ams_status_main == AMS_STATUS_MAIN_FILAMENT_CHANGE && state.ams_status_sub == 2);
    assert(!xtouch_filament_can_load(&state) && !xtouch_filament_can_unload(&state));
    auto unloaded = parse(R"({"ams_status":0,"ams":{"tray_now":"255"}})");
    xtouch_update_ams_report(state, unloaded.as<JsonObjectConst>());
    assert(state.m_tray_now == 255 && state.m_ams_id == 255 && state.m_tray_id == 255);
    assert(state.ams_exist_bits == 3 && state.tray_exist_bits == 33 && state.m_tray_tar == 5);
    assert(xtouch_filament_can_load(&state) && !xtouch_filament_can_unload(&state));
    assert(state.external_nozzle_temp_min == 210 && state.external_nozzle_temp_max == 230);
    auto external = parse(R"({"ams":{"tray_now":254}})");
    xtouch_update_ams_report(state, external.as<JsonObjectConst>());
    assert(state.m_tray_now == 254 && state.m_ams_id == 255);
    assert(!xtouch_filament_can_load(&state) && xtouch_filament_can_unload(&state));
    for (int printState : {XTOUCH_PRINT_STATUS_RUNNING, XTOUCH_PRINT_STATUS_PAUSED, XTOUCH_PRINT_STATUS_PREPARE})
    {
        state.print_status = printState;
        assert(!xtouch_filament_can_load(&state) && !xtouch_filament_can_unload(&state));
    }
    std::cout << "PASS controls: HA command envelopes, P1 firmware gate, AMS deltas and filament availability\n";
}
