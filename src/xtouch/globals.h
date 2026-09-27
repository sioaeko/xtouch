#ifndef _XLCD_GLOBALS
#define _XLCD_GLOBALS

void xtouch_globals_init()
{
    controlMode.inc = 1;
    controlMode.axis = ControlAxisXY;
    bambuStatus.m_tray_now = 255;
    bambuStatus.m_tray_tar = 255;
    bambuStatus.m_ams_id = 255;
    bambuStatus.m_tray_id = 255;
    bambuStatus.ams_status_main = AMS_STATUS_MAIN_UNKNOWN;
}

#endif
