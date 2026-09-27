#ifndef XTOUCH_FILAMENT_STATE_H
#define XTOUCH_FILAMENT_STATE_H

#include "types.h"

static inline bool xtouch_filament_set_tray(XTouchBambuStatus *status, int tray)
{
    if (!((tray >= 0 && tray <= 15) || tray == 254 || tray == 255)) return false;
    status->m_tray_now = tray;
    status->m_ams_id = tray < 16 ? tray / 4 : 255;
    status->m_tray_id = tray < 16 ? tray % 4 : 255;
    return true;
}

static inline bool xtouch_filament_idle(const XTouchBambuStatus *status)
{
    return status->printer_status_received && status->ams_status_main == AMS_STATUS_MAIN_IDLE &&
           (status->print_status == XTOUCH_PRINT_STATUS_IDLE ||
            status->print_status == XTOUCH_PRINT_STATUS_FINISHED ||
            status->print_status == XTOUCH_PRINT_STATUS_FAILED);
}

static inline bool xtouch_filament_can_load(const XTouchBambuStatus *status)
{
    return xtouch_filament_idle(status) && status->m_tray_now == 255;
}

static inline bool xtouch_filament_can_unload(const XTouchBambuStatus *status)
{
    return xtouch_filament_idle(status) &&
           ((status->m_tray_now >= 0 && status->m_tray_now <= 15) || status->m_tray_now == 254);
}

#endif
