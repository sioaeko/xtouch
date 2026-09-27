#ifndef _XLCD_SDCARD
#define _XLCD_SDCARD

#include <SD.h>
#include <SPI.h>
#include <SPIFFS.h>

// SD is an optional boot-time import/update source. Release VSPI before the
// display starts: the card and LCD use different pins on the same SPI host.
bool xtouch_sdcard_setup()
{
    SPI.begin(18, 19, 23, 5);
    if (!SD.begin(5, SPI, 4000000))
    {
        SD.end();
        SPI.end();
        ConsoleInfo.println("[XTouch][STORAGE] No SD; using internal flash");
        return false;
    }
    ConsoleInfo.printf("[XTouch][SD] Optional card mounted: %lluMB\n", SD.cardSize() / (1024 * 1024));
    return true;
}

void xtouch_sdcard_import()
{
    const char *paths[] = {xtouch_paths_config, xtouch_paths_legacy_config,
                          xtouch_paths_settings, xtouch_paths_touch};
    for (const char *path : paths)
    {
        // Internal provisioning wins over a stale card left in the slot.
        if (SPIFFS.exists(path) || !SD.exists(path)) continue;
        if ((path == xtouch_paths_config || path == xtouch_paths_legacy_config) &&
            (SPIFFS.exists(xtouch_paths_config) || SPIFFS.exists(xtouch_paths_config_backup))) continue;
        File source = SD.open(path, FILE_READ);
        if (!source || source.size() == 0 || source.size() > 8192) continue;
        File target = SPIFFS.open(path, FILE_WRITE);
        if (!target) continue;
        size_t copied = 0;
        uint8_t buffer[256];
        while (source.available())
        {
            const size_t count = source.read(buffer, sizeof(buffer));
            if (count == 0) break;
            const size_t written = target.write(buffer, count);
            copied += written;
            if (written != count) break;
        }
        target.flush();
        const bool complete = copied == source.size() && target.size() == source.size();
        source.close();
        target.close();
        if (!complete) SPIFFS.remove(path);
        else ConsoleInfo.printf("[XTouch][STORAGE] Imported %s to internal flash\n", path);
    }
}

void xtouch_sdcard_end()
{
    SD.end();
    SPI.end();
}

#endif
