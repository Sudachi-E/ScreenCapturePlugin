#pragma once
#include "common.h"
#include <wups/button_combo/defines.h>

#define ENABLED_CONFIG_DEFAULT          false
#define BUTTON_COMBO_CONFIG_DEFAULT     SAVE_BUTTON_COMBO_DEFAULT
#define RECORD_COMBO_CONFIG_DEFAULT     RECORD_BUTTON_COMBO_DEFAULT
#define RESOLUTION_CONFIG_DEFAULT       RESOLUTION_480x270
#define SECONDS_CONFIG_DEFAULT          30

#define ENABLED_CONFIG_STRING           "enabled"
#define BUTTON_COMBO_CONFIG_STRING      "saveCombo"
#define RECORD_COMBO_CONFIG_STRING      "recordCombo"
#define RESOLUTION_CONFIG_STRING        "captureResolution"
#define SECONDS_CONFIG_STRING           "ringBufferSeconds"
#define CAPTURE_SOURCE_CONFIG_STRING    "captureSource"

#define CAPTURE_SOURCE_CONFIG_DEFAULT   CAPTURE_SOURCE_DRC

void ApplyCaptureSettings();

void InitConfig();
void InitNotificationModule();
