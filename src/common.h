#pragma once

#include <coreinit/time.h>
#include <stdint.h>
#include <wups/button_combo/defines.h>

#define WIIU_VIDEO_PATH "fs:/vol/external01/wiiu/screencaptures/"

#define RING_BUFFER_SECONDS_MAX  45
#define RING_BUFFER_FRAMES_MAX   (30 * RING_BUFFER_SECONDS_MAX)

#define RESOLUTION_480x270  0
#define RESOLUTION_640x360  1
#define RESOLUTION_854x480  2
#define RESOLUTION_428x240  3

#define CAPTURE_SOURCE_DRC   0
#define CAPTURE_SOURCE_TV    1
#define CAPTURE_SOURCE_BOTH  2


#define DOWNSCALE_THRESHOLD_PX  230400

#define SAVE_BUTTON_COMBO_DEFAULT  ((WUPSButtonCombo_Buttons)( \
    WUPS_BUTTON_COMBO_BUTTON_L     | \
    WUPS_BUTTON_COMBO_BUTTON_ZR    | \
    WUPS_BUTTON_COMBO_BUTTON_MINUS))

#define RECORD_BUTTON_COMBO_DEFAULT  ((WUPSButtonCombo_Buttons)( \
    WUPS_BUTTON_COMBO_BUTTON_L     | \
    WUPS_BUTTON_COMBO_BUTTON_ZR    | \
    WUPS_BUTTON_COMBO_BUTTON_PLUS))
