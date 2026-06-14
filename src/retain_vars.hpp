#pragma once

#include <wups.h>
#include <wups/button_combo/WUPSButtonCombo.h>
#include <wups/button_combo/defines.h>
#include <gx2/surface.h>
#include <coreinit/time.h>

#include <forward_list>
#include <string>

extern bool gInForeground;

extern volatile bool gCapturing;

extern bool gIsInGame;

extern bool gEnabled;

extern int32_t gCaptureResolution;
extern int32_t gRingBufferSeconds;

extern uint32_t gCaptureWidth;
extern uint32_t gCaptureHeight;
extern uint32_t gRingBufferFrameCount;

extern GX2SurfaceFormat gDRCSurfaceFormat;

extern GX2SurfaceFormat gTVSurfaceFormat;

extern int32_t gCaptureSource;

extern uint32_t gButtonCombo;
extern WUPSButtonCombo_ComboHandle gButtonComboHandle;
extern uint32_t gRecordButtonCombo;
extern WUPSButtonCombo_ComboHandle gRecordButtonComboHandle;
extern std::forward_list<WUPSButtonComboAPI::ButtonCombo> gButtonComboInstances;

extern volatile bool gSaveRequested;

extern std::string gShortNameEn;
