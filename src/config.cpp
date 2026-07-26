#include "config.h"
#include "common.h"
#include "retain_vars.hpp"
#include "ring_buffer.h"
#include "save_thread.h"
#include "audio_capture.h"
#include "capture_utils.h"
#include "utils/logger.h"
#include "utils/StringTools.h"
#include <notifications/notifications.h>
#include <wups.h>
#include <wups/button_combo/api.h>
#include <wups/config/WUPSConfigCategory.h>
#include <wups/config/WUPSConfigItemBoolean.h>
#include <wups/config/WUPSConfigItemButtonCombo.h>
#include <wups/config/WUPSConfigItemMultipleValues.h>
#include <wups/storage.h>

WUPS_USE_STORAGE("screen_capture");

void ApplyCaptureSettings() {
    switch (gCaptureResolution) {
        case RESOLUTION_640x360:
            gCaptureWidth  = 640;
            gCaptureHeight = 360;
            break;
        case RESOLUTION_854x480:
            gCaptureWidth  = 854;
            gCaptureHeight = 480;
            break;
        case RESOLUTION_428x240:
            gCaptureWidth  = 428;
            gCaptureHeight = 240;
            break;
        case RESOLUTION_480x270:
        default:
            gCaptureWidth  = 480;
            gCaptureHeight = 270;
            break;
    }

    uint32_t seconds = (uint32_t) gRingBufferSeconds;
    uint32_t frames  = 30u * seconds;
    if (frames > RING_BUFFER_FRAMES_MAX) frames = RING_BUFFER_FRAMES_MAX;
    if (frames == 0) frames = 30 * 30;
    gRingBufferFrameCount = frames;
}

static void onSaveCombo(WUPSButtonCombo_ControllerTypes,
                        WUPSButtonCombo_ComboHandle,
                        void *) {
    if (!gEnabled) return;
    requestSave();
}

static void onRecordCombo(WUPSButtonCombo_ControllerTypes,
                          WUPSButtonCombo_ComboHandle,
                          void *) {
    bool nowEnabled = !gEnabled;

    if (nowEnabled) {
        // Start: allocate resources first, then enable hooks
        gRingBuffer.allocSlots();
        gRingBufferTV.allocSlots();
        initAudioCapture();
        startEncodeThread();
        startSaveThread();
        gEnabled   = true;
        gCapturing = true;
        OSReport("[ScreenCapture] Recording started: %u sec buffer = %u frames, quality=%d, saveOnBufferEnd=%d\n",
                 gRingBufferSeconds, gRingBufferFrameCount, gJpegQuality, gSaveOnBufferEnd);
		NotificationModule_AddInfoNotification(
		    "\ue01e ScreenCapture: Recording started.");
    } else {
        // Stop: disable hooks first, then free ALL resources
        gEnabled   = false;
        gCapturing = false;
        stopSaveThread();
        stopEncodeThread(true);
        destroyAudioCapture();
        gRingBuffer.freeSlots();
        gRingBufferTV.freeSlots();
		NotificationModule_AddInfoNotification(
		    "\ue01e ScreenCapture: Recording stopped.");
    }

    WUPSStorageAPI::Store(ENABLED_CONFIG_STRING, gEnabled);
    WUPSStorageAPI::SaveStorage();
}

static void boolItemCallback(ConfigItemBoolean *item, bool newValue) {
    if (!item || !item->identifier) return;
    if (std::string_view(item->identifier) == ENABLED_CONFIG_STRING) {
        gEnabled = newValue;
        WUPSStorageAPI::Store(item->identifier, gEnabled);
    } else if (std::string_view(item->identifier) == SAVE_ON_BUFFER_END_CONFIG_STRING) {
        gSaveOnBufferEnd = newValue;
        WUPSStorageAPI::Store(item->identifier, gSaveOnBufferEnd);
    } else if (std::string_view(item->identifier) == STOP_ON_AUTO_SAVE_CONFIG_STRING) {
        gStopOnAutoSave = newValue;
        WUPSStorageAPI::Store(item->identifier, gStopOnAutoSave);
    }
}

static void buttonComboItemChanged(ConfigItemButtonCombo *item, uint32_t newValue) {
    if (!item || !item->identifier) return;
    std::string_view id(item->identifier);
    if (id == BUTTON_COMBO_CONFIG_STRING) {
        gButtonCombo = newValue;
        WUPSStorageAPI::Store(item->identifier, gButtonCombo);
    } else if (id == RECORD_COMBO_CONFIG_STRING) {
        gRecordButtonCombo = newValue;
        WUPSStorageAPI::Store(item->identifier, gRecordButtonCombo);
    }
}

static void multipleValueCallback(ConfigItemMultipleValues *item, uint32_t newValue) {
    if (!item || !item->identifier) return;
    std::string_view id(item->identifier);

    if (id == RESOLUTION_CONFIG_STRING) {
        gCaptureResolution = (int32_t) newValue;
        WUPSStorageAPI::Store(item->identifier, gCaptureResolution);
        ApplyCaptureSettings();
    } else if (id == CAPTURE_SOURCE_CONFIG_STRING) {
        gCaptureSource = (int32_t) newValue;
        WUPSStorageAPI::Store(item->identifier, gCaptureSource);
    } else if (id == SECONDS_CONFIG_STRING) {
        gRingBufferSeconds = (int32_t) newValue;
        WUPSStorageAPI::Store(item->identifier, gRingBufferSeconds);
        ApplyCaptureSettings();
    } else if (id == JPEG_QUALITY_CONFIG_STRING) {
        gJpegQuality = (int32_t) newValue;
        WUPSStorageAPI::Store(item->identifier, gJpegQuality);
    }
}

static WUPSConfigAPICallbackStatus ConfigMenuOpenedCallback(WUPSConfigCategoryHandle rootHandle) {
    try {
        WUPSConfigCategory root(rootHandle);

        // Enabled toggle
        root.add(WUPSConfigItemBoolean::Create(
            ENABLED_CONFIG_STRING,
            "Recording enabled",
            ENABLED_CONFIG_DEFAULT, gEnabled,
            &boolItemCallback));

        // Save button combo
        root.add(WUPSConfigItemButtonCombo::Create(
            BUTTON_COMBO_CONFIG_STRING,
            "Save video combo",
            (WUPSButtonCombo_Buttons) BUTTON_COMBO_CONFIG_DEFAULT,
            gButtonComboHandle,
            &buttonComboItemChanged));

        // Record toggle button combo
        root.add(WUPSConfigItemButtonCombo::Create(
            RECORD_COMBO_CONFIG_STRING,
            "Start/Stop recording combo",
            (WUPSButtonCombo_Buttons) RECORD_COMBO_CONFIG_DEFAULT,
            gRecordButtonComboHandle,
            &buttonComboItemChanged));

        // Resolution
        constexpr WUPSConfigItemMultipleValues::ValuePair resValues[] = {
            {RESOLUTION_428x240, "428x240  (~29MB)"},
            {RESOLUTION_480x270, "480x270  (default, ~36MB)"},
            {RESOLUTION_640x360, "640x360  (~64MB)"},
            {RESOLUTION_854x480, "854x480  (~108MB)"},
        };
        root.add(WUPSConfigItemMultipleValues::CreateFromValue(
            RESOLUTION_CONFIG_STRING,
            "Capture resolution",
            RESOLUTION_CONFIG_DEFAULT, gCaptureResolution,
            resValues,
            &multipleValueCallback));

        // Buffer duration
        constexpr WUPSConfigItemMultipleValues::ValuePair durationValues[] = {
            { 15,  "15 seconds"},
            { 30,  "30 seconds  (default)"},
            { 45,  "45 seconds"},
        };
        root.add(WUPSConfigItemMultipleValues::CreateFromValue(
            SECONDS_CONFIG_STRING,
            "Buffer duration",
            SECONDS_CONFIG_DEFAULT, gRingBufferSeconds,
            durationValues,
            &multipleValueCallback));

        // Capture source
        constexpr WUPSConfigItemMultipleValues::ValuePair sourceValues[] = {
            {CAPTURE_SOURCE_DRC,  "Gamepad only"},
            {CAPTURE_SOURCE_TV,   "TV only"},
            {CAPTURE_SOURCE_BOTH, "Both (two files)"},
        };
        root.add(WUPSConfigItemMultipleValues::CreateFromValue(
            CAPTURE_SOURCE_CONFIG_STRING,
            "Capture source",
            CAPTURE_SOURCE_CONFIG_DEFAULT, gCaptureSource,
            sourceValues,
            &multipleValueCallback));

        // JPEG quality
        constexpr WUPSConfigItemMultipleValues::ValuePair qualValues[] = {
            {30, "30  (small)"},
            {40, "40"},
            { 50, "50  (2src/30s)"},
            { 60, "60  (rec.)"},
            { 70, "70"},
            { 80, "80"},
            { 90, "90  (default)"},
            {100, "100 (max qual)"},
        };
        root.add(WUPSConfigItemMultipleValues::CreateFromValue(
            JPEG_QUALITY_CONFIG_STRING,
            "JPEG quality",
            JPEG_QUALITY_CONFIG_DEFAULT, gJpegQuality,
            qualValues,
            &multipleValueCallback));

        // Save on buffer end toggle
        root.add(WUPSConfigItemBoolean::Create(
            SAVE_ON_BUFFER_END_CONFIG_STRING,
            "Save on buffer end",
            SAVE_ON_BUFFER_END_DEFAULT, gSaveOnBufferEnd,
            &boolItemCallback));

        // Stop recording after auto-save toggle
        root.add(WUPSConfigItemBoolean::Create(
            STOP_ON_AUTO_SAVE_CONFIG_STRING,
            "Stop recording after saving",
            STOP_ON_AUTO_SAVE_DEFAULT, gStopOnAutoSave,
            &boolItemCallback));

	} catch (std::exception &e) {
	    OSReport("ScreenCapture config exception: %s\n", e.what());
	    return WUPSCONFIG_API_CALLBACK_RESULT_ERROR;
	}
    return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
}

static void ConfigMenuClosedCallback() {
    WUPSStorageAPI::SaveStorage();
}

void InitNotificationModule() {
    NotificationModule_SetDefaultValue(
        NOTIFICATION_MODULE_NOTIFICATION_TYPE_INFO,
        NOTIFICATION_MODULE_DEFAULT_OPTION_KEEP_UNTIL_SHOWN, true);
    NotificationModule_SetDefaultValue(
        NOTIFICATION_MODULE_NOTIFICATION_TYPE_INFO,
        NOTIFICATION_MODULE_DEFAULT_OPTION_DURATION_BEFORE_FADE_OUT, 5.0f);
    NotificationModule_SetDefaultValue(
        NOTIFICATION_MODULE_NOTIFICATION_TYPE_ERROR,
        NOTIFICATION_MODULE_DEFAULT_OPTION_KEEP_UNTIL_SHOWN, true);
    NotificationModule_SetDefaultValue(
        NOTIFICATION_MODULE_NOTIFICATION_TYPE_ERROR,
        NOTIFICATION_MODULE_DEFAULT_OPTION_DURATION_BEFORE_FADE_OUT, 8.0f);
}

	void InitConfig() {
	    WUPSConfigAPIOptionsV1 opts = {.name = "Screen Capture"};
    if (WUPSConfigAPI_Init(opts, ConfigMenuOpenedCallback, ConfigMenuClosedCallback)
            != WUPSCONFIG_API_RESULT_SUCCESS) {
		DEBUG_FUNCTION_LINE("ScreenCapture: WUPSConfigAPI_Init failed\n");
    }

    WUPSStorageAPI::GetOrStoreDefault(ENABLED_CONFIG_STRING,
                                      gEnabled, ENABLED_CONFIG_DEFAULT);
    WUPSStorageAPI::GetOrStoreDefault<uint32_t>(BUTTON_COMBO_CONFIG_STRING,
                                                gButtonCombo,
                                                (uint32_t) BUTTON_COMBO_CONFIG_DEFAULT);
    if (gButtonCombo == 0) gButtonCombo = (uint32_t) BUTTON_COMBO_CONFIG_DEFAULT;

    WUPSStorageAPI::GetOrStoreDefault<uint32_t>(RECORD_COMBO_CONFIG_STRING,
                                                gRecordButtonCombo,
                                                (uint32_t) RECORD_COMBO_CONFIG_DEFAULT);
    if (gRecordButtonCombo == 0) gRecordButtonCombo = (uint32_t) RECORD_COMBO_CONFIG_DEFAULT;

    WUPSStorageAPI::GetOrStoreDefault(RESOLUTION_CONFIG_STRING,
                                      gCaptureResolution, RESOLUTION_CONFIG_DEFAULT);
    WUPSStorageAPI::GetOrStoreDefault(SECONDS_CONFIG_STRING,
                                      gRingBufferSeconds, SECONDS_CONFIG_DEFAULT);
    WUPSStorageAPI::GetOrStoreDefault(CAPTURE_SOURCE_CONFIG_STRING,
                                      gCaptureSource, CAPTURE_SOURCE_CONFIG_DEFAULT);
    WUPSStorageAPI::GetOrStoreDefault(SAVE_ON_BUFFER_END_CONFIG_STRING,
                                      gSaveOnBufferEnd, SAVE_ON_BUFFER_END_DEFAULT);
    WUPSStorageAPI::GetOrStoreDefault(STOP_ON_AUTO_SAVE_CONFIG_STRING,
                                      gStopOnAutoSave, STOP_ON_AUTO_SAVE_DEFAULT);
    WUPSStorageAPI::GetOrStoreDefault(JPEG_QUALITY_CONFIG_STRING,
                                      gJpegQuality, JPEG_QUALITY_CONFIG_DEFAULT);
    WUPSStorageAPI::SaveStorage();

    if (gCaptureResolution < 0 || gCaptureResolution > 2) gCaptureResolution = RESOLUTION_CONFIG_DEFAULT;
    if (gRingBufferSeconds < 30 || gRingBufferSeconds > RING_BUFFER_SECONDS_MAX)
        gRingBufferSeconds = SECONDS_CONFIG_DEFAULT;
    if (gCaptureSource < 0 || gCaptureSource > 2) gCaptureSource = CAPTURE_SOURCE_CONFIG_DEFAULT;

    ApplyCaptureSettings();

    WUPSButtonCombo_ComboStatus comboStatus;
    WUPSButtonCombo_Error       comboError = WUPS_BUTTON_COMBO_ERROR_UNKNOWN_ERROR;
	
	auto comboOpt = WUPSButtonComboAPI::CreateComboPressDown(
	    "ScreenCapture save combo",
        (WUPSButtonCombo_Buttons) gButtonCombo,
        onSaveCombo,
        nullptr,
        comboStatus,
        comboError);

	    if (!comboOpt || comboError != WUPS_BUTTON_COMBO_ERROR_SUCCESS) {
	        auto errMsg = string_format(
	            "ScreenCapture: Failed to register button combo: %s",
            WUPSButtonComboAPI::GetStatusStr(comboError));
        DEBUG_FUNCTION_LINE("%s\n", errMsg.c_str());
        NotificationModule_AddErrorNotification(errMsg.c_str());
    } else {
        gButtonComboHandle = comboOpt->getHandle();
        gButtonComboInstances.emplace_front(std::move(*comboOpt));

	        if (comboStatus == WUPS_BUTTON_COMBO_COMBO_STATUS_CONFLICT) {
	            NotificationModule_AddInfoNotification(
	                "ScreenCapture: Button combo conflict — please reassign in config menu.");
	        }
	    }

    WUPSButtonCombo_ComboStatus recordComboStatus;
    WUPSButtonCombo_Error       recordComboError = WUPS_BUTTON_COMBO_ERROR_UNKNOWN_ERROR;
	
	auto recordComboOpt = WUPSButtonComboAPI::CreateComboPressDown(
	    "ScreenCapture record toggle combo",
        (WUPSButtonCombo_Buttons) gRecordButtonCombo,
        onRecordCombo,
        nullptr,
        recordComboStatus,
        recordComboError);

	    if (!recordComboOpt || recordComboError != WUPS_BUTTON_COMBO_ERROR_SUCCESS) {
	        auto errMsg = string_format(
	            "ScreenCapture: Failed to register record combo: %s",
            WUPSButtonComboAPI::GetStatusStr(recordComboError));
        DEBUG_FUNCTION_LINE("%s\n", errMsg.c_str());
        NotificationModule_AddErrorNotification(errMsg.c_str());
    } else {
        gRecordButtonComboHandle = recordComboOpt->getHandle();
        gButtonComboInstances.emplace_front(std::move(*recordComboOpt));

	        if (recordComboStatus == WUPS_BUTTON_COMBO_COMBO_STATUS_CONFLICT) {
	            NotificationModule_AddInfoNotification(
	                "ScreenCapture: Record combo conflict — please reassign in config menu.");
	        }
	    }
}
