#include "common.h"
#include "retain_vars.hpp"
#include "capture_utils.h"
#include "audio_capture.h"
#include "utils/logger.h"
#include <gx2/surface.h>
#include <gx2/swap.h>
#include <coreinit/debug.h>
#include <coreinit/time.h>
#include <sndcore2/core.h>
#include <wups.h>

static uint64_t sMinTicks         = 0;
static uint64_t sLastDRCTick      = 0;
static uint64_t sLastTVTick       = 0;

void resetCaptureTiming() {
    sMinTicks      = 0;
    sLastDRCTick   = 0;
    sLastTVTick    = 0;
}

DECL_FUNCTION(void, GX2SwapScanBuffers) {
    real_GX2SwapScanBuffers();
}

DECL_FUNCTION(void, GX2CopyColorBufferToScanBuffer,
              const GX2ColorBuffer *colorBuffer,
              GX2ScanTarget         scan_target) {

    if (gEnabled && gCapturing && colorBuffer != nullptr) {
        if (sMinTicks == 0) {
            sMinTicks = OSSecondsToTicks(1) / 30u;
        }

        uint64_t now = (uint64_t) OSGetTime();

        bool captureDRC = (gCaptureSource == CAPTURE_SOURCE_DRC ||
                           gCaptureSource == CAPTURE_SOURCE_BOTH);
        bool captureTV  = (gCaptureSource == CAPTURE_SOURCE_TV  ||
                           gCaptureSource == CAPTURE_SOURCE_BOTH);

        if (captureDRC && scan_target == GX2_SCAN_TARGET_DRC0) {
            if (now - sLastDRCTick >= sMinTicks) {
                sLastDRCTick = now;
                { static uint32_t t = 0; if ((++t % 30) == 1)
                    OSReport("[ScreenCapture] GX2Copy: DRC frame\n"); }
                captureFrame((GX2ColorBuffer *) colorBuffer, gDRCSurfaceFormat, false);
            }
        }

        if (captureTV && scan_target == GX2_SCAN_TARGET_TV) {
            if (now - sLastTVTick >= sMinTicks) {
                sLastTVTick = now;
                { static uint32_t t = 0; if ((++t % 30) == 1)
                    OSReport("[ScreenCapture] GX2Copy: TV frame\n"); }
                captureFrame((GX2ColorBuffer *) colorBuffer, gTVSurfaceFormat, true);
            }
        }
    }

    real_GX2CopyColorBufferToScanBuffer(colorBuffer, scan_target);
}

DECL_FUNCTION(void, GX2SetDRCBuffer,
              void *buffer, uint32_t buffer_size, uint32_t drc_mode,
              GX2SurfaceFormat surface_format, GX2BufferingMode buffering_mode) {
    gDRCSurfaceFormat = surface_format;
    real_GX2SetDRCBuffer(buffer, buffer_size, drc_mode, surface_format, buffering_mode);
}

DECL_FUNCTION(void, GX2SetTVBuffer,
              void *buffer, uint32_t buffer_size, int32_t tv_render_mode,
              GX2SurfaceFormat surface_format, GX2BufferingMode buffering_mode) {
    gTVSurfaceFormat = surface_format;
    real_GX2SetTVBuffer(buffer, buffer_size, tv_render_mode, surface_format, buffering_mode);
}

static int sAIInitDMACount = 0;
DECL_FUNCTION(void, AIInitDMA, void *buffer, uint32_t size) {
    sAIInitDMACount++;
    if (gEnabled) { static uint32_t t = 0; if ((++t % 60) == 1)
        OSReport("[FuncPatch] AIInitDMA #%d: buf=%p size=%u gEnabled=%d\n",
                 sAIInitDMACount, buffer, size, gEnabled); }
    if (gEnabled && buffer && size > 0) captureAudioBuffer(AUDIO_SRC_TV, buffer, size);
    real_AIInitDMA(buffer, size);
}

static int sAI2InitDMACount = 0;
DECL_FUNCTION(void, AI2InitDMA, void *buffer, uint32_t size) {
    sAI2InitDMACount++;
    if (gEnabled) { static uint32_t t = 0; if ((++t % 60) == 1)
        OSReport("[FuncPatch] AI2InitDMA #%d: buf=%p size=%u gEnabled=%d\n",
                 sAI2InitDMACount, buffer, size, gEnabled); }
    if (gEnabled && buffer && size > 0) captureAudioBuffer(AUDIO_SRC_DRC, buffer, size);
    real_AI2InitDMA(buffer, size);
}

static int sAI2InitDMA2Count = 0;
DECL_FUNCTION(void, AI2InitDMA2, void *buffer, uint32_t size) {
    sAI2InitDMA2Count++;
    if (gEnabled) { static uint32_t t = 0; if ((++t % 60) == 1)
        OSReport("[FuncPatch] AI2InitDMA2 #%d: buf=%p size=%u gEnabled=%d\n",
                 sAI2InitDMA2Count, buffer, size, gEnabled); }
    if (gEnabled && buffer && size > 0) captureAudioBuffer(AUDIO_SRC_DRC, buffer, size);
    real_AI2InitDMA2(buffer, size);
}

static int sAIInitDRCDMACount = 0;
DECL_FUNCTION(void, AIInitDRCDMA, void *buffer, uint32_t size) {
    sAIInitDRCDMACount++;
    if (gEnabled) { static uint32_t t = 0; if ((++t % 60) == 1)
        OSReport("[FuncPatch] AIInitDRCDMA #%d: buf=%p size=%u gEnabled=%d\n",
                 sAIInitDRCDMACount, buffer, size, gEnabled); }
    if (gEnabled && buffer && size > 0) captureAudioBuffer(AUDIO_SRC_DRC, buffer, size);
    real_AIInitDRCDMA(buffer, size);
}

static int sAIInitTVOrDRCDMACount = 0;
DECL_FUNCTION(void, AIInitTVOrDRCDMA, void *buffer, uint32_t size) {
    sAIInitTVOrDRCDMACount++;
    if (gEnabled) { static uint32_t t = 0; if ((++t % 60) == 1)
        OSReport("[FuncPatch] AIInitTVOrDRCDMA #%d: buf=%p size=%u gEnabled=%d\n",
                 sAIInitTVOrDRCDMACount, buffer, size, gEnabled); }
    if (gEnabled && buffer && size > 0) {
        captureAudioBuffer(AUDIO_SRC_TV,  buffer, size);
        captureAudioBuffer(AUDIO_SRC_DRC, buffer, size);
    }
    real_AIInitTVOrDRCDMA(buffer, size);
}

static void *sOrigDRCCallback = nullptr;
static void ourDRCCallback(void *buffer, uint32_t size) {
    if (gEnabled && buffer && size > 0) captureAudioBuffer(AUDIO_SRC_DRC, buffer, size);
    if (sOrigDRCCallback) ((void (*)(void *, uint32_t))sOrigDRCCallback)(buffer, size);
}
static int sAIDRCRegisterDMASourceCount = 0;
DECL_FUNCTION(void, AIDRCRegisterDMASource, void *callback, void *userdata) {
    sAIDRCRegisterDMASourceCount++;
    if (sAIDRCRegisterDMASourceCount <= 3) {
        OSReport("[FuncPatch] AIDRCRegisterDMASource #%d: cb=%p ud=%p\n",
                 sAIDRCRegisterDMASourceCount, callback, userdata);
    }
    sOrigDRCCallback = callback;
    real_AIDRCRegisterDMASource((void *)ourDRCCallback, userdata);
}



WUPS_MUST_REPLACE(AIInitDMA,                       WUPS_LOADER_LIBRARY_SND_CORE, AIInitDMA);
WUPS_MUST_REPLACE(AI2InitDMA,                      WUPS_LOADER_LIBRARY_SND_CORE, AI2InitDMA);
WUPS_MUST_REPLACE(AI2InitDMA2,                     WUPS_LOADER_LIBRARY_SNDCORE2, AI2InitDMA);
WUPS_MUST_REPLACE(AIInitDRCDMA,                    WUPS_LOADER_LIBRARY_SND_CORE, AIInitDRCDMA);
WUPS_MUST_REPLACE(AIInitTVOrDRCDMA,                WUPS_LOADER_LIBRARY_SND_CORE, AIInitTVOrDRCDMA);
WUPS_MUST_REPLACE(AIDRCRegisterDMASource,           WUPS_LOADER_LIBRARY_SND_CORE, AIDRCRegisterDMASource);
WUPS_MUST_REPLACE(GX2SwapScanBuffers,              WUPS_LOADER_LIBRARY_GX2, GX2SwapScanBuffers);
WUPS_MUST_REPLACE(GX2CopyColorBufferToScanBuffer,  WUPS_LOADER_LIBRARY_GX2, GX2CopyColorBufferToScanBuffer);
WUPS_MUST_REPLACE(GX2SetDRCBuffer,                 WUPS_LOADER_LIBRARY_GX2, GX2SetDRCBuffer);
WUPS_MUST_REPLACE(GX2SetTVBuffer,                  WUPS_LOADER_LIBRARY_GX2, GX2SetTVBuffer);
