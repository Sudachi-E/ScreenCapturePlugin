#include "retain_vars.hpp"
#include "common.h"

bool gInForeground __attribute__((section(".data"))) = false;

volatile bool gCapturing __attribute__((section(".data"))) = false;

bool gIsInGame __attribute__((section(".data"))) = false;

bool gEnabled __attribute__((section(".data"))) = false;

int32_t  gCaptureResolution   __attribute__((section(".data"))) = RESOLUTION_480x270;
int32_t  gRingBufferSeconds   __attribute__((section(".data"))) = 30;
int32_t  gJpegQuality         __attribute__((section(".data"))) = 90;

uint32_t gCaptureWidth        __attribute__((section(".data"))) = 480;
uint32_t gCaptureHeight       __attribute__((section(".data"))) = 270;
uint32_t gRingBufferFrameCount __attribute__((section(".data"))) = 30 * 30;

GX2SurfaceFormat gDRCSurfaceFormat __attribute__((section(".data"))) = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
GX2SurfaceFormat gTVSurfaceFormat  __attribute__((section(".data"))) = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;

int32_t gCaptureSource __attribute__((section(".data"))) = CAPTURE_SOURCE_DRC;

bool gSaveOnBufferEnd __attribute__((section(".data"))) = false;
bool gStopOnAutoSave __attribute__((section(".data"))) = false;
volatile bool gUsePendingSave __attribute__((section(".data"))) = false;
uint64_t gCycleAudioWritePosDRC __attribute__((section(".data"))) = 0;
uint64_t gCycleAudioWritePosTV __attribute__((section(".data"))) = 0;

int16_t *gPendingAudioDRC __attribute__((section(".data"))) = nullptr;
size_t   gPendingAudioDRCSamps __attribute__((section(".data"))) = 0;
uint32_t gPendingAudioDRCRate __attribute__((section(".data"))) = 0;
int16_t *gPendingAudioTV __attribute__((section(".data"))) = nullptr;
size_t   gPendingAudioTVSamps __attribute__((section(".data"))) = 0;
uint32_t gPendingAudioTVRate __attribute__((section(".data"))) = 0;

uint32_t gButtonCombo __attribute__((section(".data"))) = (uint32_t) SAVE_BUTTON_COMBO_DEFAULT;

WUPSButtonCombo_ComboHandle gButtonComboHandle(nullptr);

uint32_t gRecordButtonCombo __attribute__((section(".data"))) = (uint32_t) RECORD_BUTTON_COMBO_DEFAULT;

WUPSButtonCombo_ComboHandle gRecordButtonComboHandle(nullptr);

std::forward_list<WUPSButtonComboAPI::ButtonCombo> gButtonComboInstances;

volatile bool gSaveRequested __attribute__((section(".data"))) = false;

std::string gShortNameEn;
uint64_t gTitleID __attribute__((section(".data"))) = 0;
