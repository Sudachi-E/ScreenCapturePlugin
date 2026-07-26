#include "capture_utils.h"
#include "common.h"
#include "ring_buffer.h"
#include "retain_vars.hpp"
#include "audio_capture.h"
#include "save_thread.h"
#include "utils/logger.h"
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/state.h>
#include <gx2/surface.h>
#include <memory/mappedmemory.h>
#include <coreinit/thread.h>
#include <coreinit/messagequeue.h>
#include <coreinit/atomic.h>
#include <coreinit/cache.h>
#include <coreinit/time.h>
#include <coreinit/debug.h>
#include <malloc.h>
#include <string.h>
#include <turbojpeg.h>
#include <notifications/notifications.h>

#ifdef __cplusplus
extern "C" {
#endif
void GX2ResolveAAColorBuffer(const GX2ColorBuffer *srcColorBuffer,
                              GX2Surface           *dstSurface,
                              uint32_t              dstMip,
                              uint32_t              dstSlice);
#ifdef __cplusplus
}
#endif

RingBuffer gRingBufferTV;

static const uint8_t sRGBGammaLUT[256] = {
    0x00, 0x0C, 0x15, 0x1C, 0x21, 0x26, 0x2A, 0x2E, 0x31, 0x34, 0x37, 0x3A, 0x3D, 0x3F, 0x42, 0x44,
    0x46, 0x49, 0x4B, 0x4D, 0x4F, 0x51, 0x52, 0x54, 0x56, 0x58, 0x59, 0x5B, 0x5D, 0x5E, 0x60, 0x61,
    0x63, 0x64, 0x66, 0x67, 0x68, 0x6A, 0x6B, 0x6D, 0x6E, 0x6F, 0x70, 0x72, 0x73, 0x74, 0x75, 0x76,
    0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
    0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8E, 0x8F, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9D, 0x9E, 0x9F, 0xA0, 0xA1, 0xA1, 0xA2, 0xA3, 0xA4,
    0xA5, 0xA5, 0xA6, 0xA7, 0xA8, 0xA8, 0xA9, 0xAA, 0xAB, 0xAB, 0xAC, 0xAD, 0xAE, 0xAE, 0xAF, 0xB0,
    0xB0, 0xB1, 0xB2, 0xB3, 0xB3, 0xB4, 0xB5, 0xB5, 0xB6, 0xB7, 0xB7, 0xB8, 0xB9, 0xB9, 0xBA, 0xBB,
    0xBB, 0xBC, 0xBD, 0xBD, 0xBE, 0xBF, 0xBF, 0xC0, 0xC1, 0xC1, 0xC2, 0xC2, 0xC3, 0xC4, 0xC4, 0xC5,
    0xC5, 0xC6, 0xC7, 0xC7, 0xC8, 0xC9, 0xC9, 0xCA, 0xCA, 0xCB, 0xCC, 0xCC, 0xCD, 0xCD, 0xCE, 0xCE,
    0xCF, 0xD0, 0xD0, 0xD1, 0xD1, 0xD2, 0xD2, 0xD3, 0xD4, 0xD4, 0xD5, 0xD5, 0xD6, 0xD6, 0xD7, 0xD7,
    0xD8, 0xD9, 0xD9, 0xDA, 0xDA, 0xDB, 0xDB, 0xDC, 0xDC, 0xDD, 0xDD, 0xDE, 0xDE, 0xDF, 0xDF, 0xE0,
    0xE1, 0xE1, 0xE2, 0xE2, 0xE3, 0xE3, 0xE4, 0xE4, 0xE5, 0xE5, 0xE6, 0xE6, 0xE7, 0xE7, 0xE8, 0xE8,
    0xE9, 0xE9, 0xEA, 0xEA, 0xEB, 0xEB, 0xEC, 0xEC, 0xED, 0xED, 0xED, 0xEE, 0xEE, 0xEF, 0xEF, 0xF0,
    0xF0, 0xF1, 0xF1, 0xF2, 0xF2, 0xF3, 0xF3, 0xF4, 0xF4, 0xF5, 0xF5, 0xF5, 0xF6, 0xF6, 0xF7, 0xF7,
    0xF8, 0xF8, 0xF9, 0xF9, 0xFA, 0xFA, 0xFB, 0xFB, 0xFB, 0xFC, 0xFC, 0xFD, 0xFD, 0xFE, 0xFE, 0xFE
};

#define PROF_LOG_INTERVAL 60

struct CaptureProf {
    uint32_t  frameCount;
    uint32_t  skipCount;
    uint64_t  copyUs;
    uint64_t  captureUs;
    uint32_t  encCount;
    uint64_t  encWaitUs;
    uint64_t  gammaUs;
    uint64_t  jpegUs;
    uint64_t  encUs;
};

static CaptureProf sProf;

static void profLog() {
    if (sProf.frameCount == 0) return;
    uint32_t n = sProf.frameCount;
    uint32_t e = sProf.encCount ? sProf.encCount : 1;
    OSReport("[ScreenCapture] === Profiler ===\n"
             "  Render:  avg %llu us/frame  (sync %llu us)  skipped=%u\n"
             "  Encode:  avg %llu us/frame  (wait %llu, gamma %llu, jpeg %llu)\n"
             "  Frames:  captured=%u  encoded=%u\n",
             (unsigned long long)(sProf.captureUs / n),
             (unsigned long long)(sProf.copyUs / n),
             (unsigned) sProf.skipCount,
             (unsigned long long)(sProf.encUs / e),
             (unsigned long long)(sProf.encWaitUs / e),
             (unsigned long long)(sProf.gammaUs / e),
             (unsigned long long)(sProf.jpegUs / e),
             (unsigned) sProf.frameCount,
             (unsigned) sProf.encCount);
    memset(&sProf, 0, sizeof(sProf));
}

#define SURFACE_POOL_SIZE 4

struct PoolSurface {
    GX2ColorBuffer buffer;
    GX2ColorBuffer aaResolve;
    bool            busy;
    OSTime          gpuTimestamp;
    uint64_t        audioWritePos;
    bool            needsSRGB;
};

static PoolSurface  sPool[SURFACE_POOL_SIZE];
static uint32_t     sPoolWriteIdx = 0;
static bool         sPoolInited   = false;

static int32_t sPendingSyncIdx[2] = { -1, -1 };

static bool allocPoolSurface(PoolSurface *ps) {
    GX2ColorBuffer *buf = &ps->buffer;
    memset(buf, 0, sizeof(*buf));
    buf->surface.use       = (GX2SurfaceUse)(GX2_SURFACE_USE_COLOR_BUFFER | GX2_SURFACE_USE_TEXTURE);
    buf->surface.dim       = GX2_SURFACE_DIM_TEXTURE_2D;
    buf->surface.width     = gCaptureWidth;
    buf->surface.height    = gCaptureHeight;
    buf->surface.depth     = 1;
    buf->surface.mipLevels = 1;
    buf->surface.format    = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    buf->surface.aa        = GX2_AA_MODE1X;
    buf->surface.tileMode  = GX2_TILE_MODE_LINEAR_ALIGNED;
    buf->viewNumSlices     = 1;
    GX2CalcSurfaceSizeAndAlignment(&buf->surface);
    GX2InitColorBufferRegs(buf);
    buf->surface.image = MEMAllocFromMappedMemoryForGX2Ex(
        buf->surface.imageSize, buf->surface.alignment);
    if (!buf->surface.image) {
        DEBUG_FUNCTION_LINE("allocPoolSurface: failed to alloc color buffer image (%u bytes)\n",
                            buf->surface.imageSize);
        return false;
    }

    GX2Surface *aa = &ps->aaResolve.surface;
    memset(aa, 0, sizeof(*aa));
    aa->use       = (GX2SurfaceUse)(GX2_SURFACE_USE_COLOR_BUFFER | GX2_SURFACE_USE_TEXTURE);
    aa->dim       = GX2_SURFACE_DIM_TEXTURE_2D;
    aa->width     = gCaptureWidth;
    aa->height    = gCaptureHeight;
    aa->depth     = 1;
    aa->mipLevels = 1;
    aa->format    = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    aa->aa        = GX2_AA_MODE1X;
    aa->tileMode  = GX2_TILE_MODE_LINEAR_ALIGNED;
    GX2CalcSurfaceSizeAndAlignment(aa);
    aa->image     = MEMAllocFromMappedMemoryForGX2Ex(aa->imageSize, aa->alignment);
    if (!aa->image) {
        DEBUG_FUNCTION_LINE("allocPoolSurface: failed to alloc AA resolve image (%u bytes)\n",
                            aa->imageSize);
        MEMFreeToMappedMemory(buf->surface.image);
        buf->surface.image = nullptr;
        return false;
    }

    ps->busy          = false;
    ps->gpuTimestamp  = 0;
    ps->audioWritePos = 0;
    ps->needsSRGB     = false;
    return true;
}

static void freePoolSurface(PoolSurface *ps) {
    if (ps->buffer.surface.image) {
        MEMFreeToMappedMemory(ps->buffer.surface.image);
        ps->buffer.surface.image = nullptr;
    }
    if (ps->aaResolve.surface.image) {
        MEMFreeToMappedMemory(ps->aaResolve.surface.image);
        ps->aaResolve.surface.image = nullptr;
    }
}

static void initSurfacePool() {
    if (sPoolInited) return;
    for (uint32_t i = 0; i < SURFACE_POOL_SIZE; i++) {
        if (!allocPoolSurface(&sPool[i])) {
            // Free any surfaces already allocated and bail out
            for (uint32_t j = 0; j < i; j++) {
                freePoolSurface(&sPool[j]);
            }
            OSReport("initSurfacePool: out of mapped memory, capture disabled\n");
            return;
        }
    }
    sPoolWriteIdx = 0;
    GX2DrawDone();
    sPoolInited = true;
}

static void destroySurfacePool() {
    if (!sPoolInited) return;
    for (uint32_t i = 0; i < SURFACE_POOL_SIZE; i++) {
        freePoolSurface(&sPool[i]);
    }
    sPoolInited = false;
    sPendingSyncIdx[0] = -1;
    sPendingSyncIdx[1] = -1;
}

#define ENCODE_QUEUE_DEPTH SURFACE_POOL_SIZE

static OSThread       *sEncodeThread  = nullptr;
static uint8_t        *sEncodeStack   = nullptr;
static OSMessageQueue  sEncodeQueue;
static OSMessage       sEncodeMessages[ENCODE_QUEUE_DEPTH + 1];
static bool            sEncodeRunning = false;

static volatile int32_t sEncodeInFlight = 0;

#define ENCODE_CMD_STOP  ((void *) 0xDEAD0001u)

struct CaptureSurface {
    uint32_t         poolIdx;
    GX2ColorBuffer  *buffer;
    uint32_t         width;
    uint32_t         height;
    uint32_t         pitch;
    uint64_t         timestamp;
    uint64_t         audioWritePos;
    bool             isTV;
    bool             needsSRGB;
};

static void issueAsyncCopy(GX2ColorBuffer *src, PoolSurface *ps) {
    GX2ColorBuffer *dst = &ps->buffer;
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, dst->surface.image, dst->surface.imageSize);

    if (src->surface.aa == GX2_AA_MODE1X) {
        GX2CopySurface(&src->surface, src->viewMip, src->viewFirstSlice,
                       &dst->surface, 0, 0);
    } else {
        GX2Surface *aaDst = &ps->aaResolve.surface;
        GX2ResolveAAColorBuffer(src, aaDst, 0, 0);
        GX2CopySurface(aaDst, 0, 0, &dst->surface, 0, 0);
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER,
                  dst->surface.image, dst->surface.imageSize);
}

static void syncAndSend(int srcIdx) {
    int pIdx = sPendingSyncIdx[srcIdx];
    if (pIdx < 0) return;

    PoolSurface *ps = &sPool[pIdx];
    GX2ColorBuffer *buf = &ps->buffer;

    { static uint32_t syncThrottle = 0; if ((++syncThrottle % 30) == 1)
        OSReport("[ScreenCapture] syncAndSend: src=%d pIdx=%d ts=%llu\n",
                 srcIdx, pIdx, (unsigned long long)ps->gpuTimestamp); }

    if (ps->gpuTimestamp > 0) {
        OSTime tCopy = OSGetTime();
        GX2WaitTimeStamp(ps->gpuTimestamp);
        sProf.copyUs += OSTicksToMicroseconds(OSGetTime() - tCopy);
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, buf->surface.image, buf->surface.imageSize);

    auto *cs = (CaptureSurface *) malloc(sizeof(CaptureSurface));
    if (cs) {
        cs->poolIdx      = (uint32_t)pIdx;
        cs->buffer       = buf;
        cs->width        = buf->surface.width;
        cs->height       = buf->surface.height;
        cs->pitch        = buf->surface.pitch;
        cs->timestamp    = OSGetTime();
        cs->audioWritePos = ps->audioWritePos;
        cs->isTV         = (srcIdx == 1);
        cs->needsSRGB    = ps->needsSRGB;

        ps->busy = true;

        OSMessage msg;
        msg.message = (void *) cs;
        OSAddAtomic(&sEncodeInFlight, 1);
        if (!OSSendMessage(&sEncodeQueue, &msg, OS_MESSAGE_FLAGS_NONE)) {
            OSReport("[ScreenCapture] syncAndSend: QUEUE FULL! src=%d\n", srcIdx);
            OSAddAtomic(&sEncodeInFlight, -1);
            ps->busy = false;
            free(cs);
        }
    } else {
        OSReport("[ScreenCapture] syncAndSend: malloc FAILED for CaptureSurface\n");
    }

    sPendingSyncIdx[srcIdx] = -1;
}

static void flushPendingSync() {
    syncAndSend(0);
    syncAndSend(1);
}

static int32_t encodeThreadEntry([[maybe_unused]] int argc, const char **argv) {
    auto *queue = (OSMessageQueue *) argv;

    tjhandle tj = tjInitCompress();
    if (!tj) {
        DEBUG_FUNCTION_LINE("encodeThread: tjInitCompress failed\n");
        return 1;
    }

    uint32_t prevWidth  = 0;
    uint32_t prevHeight = 0;
    unsigned long maxJpegSize = 0;
    uint8_t *jpegBuf = nullptr;

    OSTime tWait = OSGetTime();
    OSMessage msg;
    while (OSReceiveMessage(queue, &msg, OS_MESSAGE_FLAGS_BLOCKING)) {
        sProf.encWaitUs += OSTicksToMicroseconds(OSGetTime() - tWait);
        { static uint32_t hb = 0; if ((++hb % 60) == 1)
            OSReport("[ScreenCapture] Encode: alive, ringBuf count=%u\n", gRingBuffer.count()); }
        if (msg.message == ENCODE_CMD_STOP) {
            OSReport("[ScreenCapture] Encode: received STOP\n");
            break;
        }

        auto *cs = (CaptureSurface *) msg.message;
        if (!cs) {
            OSReport("[ScreenCapture] Encode: NULL cs!\n");
            tWait = OSGetTime(); continue;
        }

        uint8_t *px = (uint8_t *) cs->buffer->surface.image;
        uint32_t  w = cs->width;
        uint32_t  h = cs->height;
        uint32_t  p = cs->pitch;

        OSTime tEncFrame = OSGetTime();

        if (cs->needsSRGB) {
            OSTime tGam = OSGetTime();
            uint32_t totalPixels = p * h;
            uint32_t *px32 = (uint32_t *) px;
            for (uint32_t i = 0; i < totalPixels; i++) {
                uint32_t pxl = px32[i];
                uint8_t r = sRGBGammaLUT[(pxl >> 24) & 0xFF];
                uint8_t g = sRGBGammaLUT[(pxl >> 16) & 0xFF];
                uint8_t b = sRGBGammaLUT[(pxl >>  8) & 0xFF];
                px32[i] = (r << 24) | (g << 16) | (b << 8) | (pxl & 0xFF);
            }
            sProf.gammaUs += OSTicksToMicroseconds(OSGetTime() - tGam);
        }

        OSTime tJpeg = OSGetTime();

        if (w != prevWidth || h != prevHeight) {
            prevWidth  = w;
            prevHeight = h;
            maxJpegSize = tjBufSize((int)w, (int)h, TJSAMP_420);
            OSReport("[SC] Encode: tjBufSize(%u,%u,420)=%lu\n", w, h, maxJpegSize);
            if (jpegBuf) free(jpegBuf);
            jpegBuf = (uint8_t *) malloc(maxJpegSize);
            if (!jpegBuf) {
                OSReport("[ScreenCapture] Encode: JPEG BUF MALLOC FAILED! size=%lu w=%u h=%u\n",
                         maxJpegSize, w, h);
                sPool[cs->poolIdx].busy = false;
                free(cs);
                sProf.jpegUs += OSTicksToMicroseconds(OSGetTime() - tJpeg);
                sProf.encUs  += OSTicksToMicroseconds(OSGetTime() - tEncFrame);
                tWait = OSGetTime(); continue;
            }
        }

        unsigned long jpegSize = 0;
        unsigned char *outBuf = jpegBuf;

        OSTime tJpegStart = OSGetTime();
        int ret = tjCompress2(tj, px, (int) w, (int)(p * 4), (int) h,
                              TJPF_RGBX, &outBuf, &jpegSize,
                               TJSAMP_420, gJpegQuality,
                              TJFLAG_NOREALLOC);
        { static uint32_t jlog = 0; if ((++jlog % 30) == 1) {
            OSTime tJpegEnd = OSGetTime();
            OSReport("[ScreenCapture] Encode: JPEG %ux%u took %llu us, size=%lu\n",
                     w, h, (unsigned long long)OSTicksToMicroseconds(tJpegEnd - tJpegStart), jpegSize);
        }}

        sPool[cs->poolIdx].busy = false;

        if (ret != 0 || jpegSize == 0) {
            OSReport("[ScreenCapture] Encode: JPEG FAILED ret=%d size=%lu "
                     "w=%u h=%u pitch=%u maxSize=%lu jpegBuf=%p outBuf=%p px=%p\n",
                     ret, jpegSize, w, h, p, maxJpegSize, (void*)jpegBuf, (void*)outBuf, (void*)px);
            const char *tjErr = tjGetErrorStr();
            if (tjErr) OSReport("[ScreenCapture] Encode: tjError: %s\n", tjErr);

            // If tjCompress2 ran out of memory, free the ring buffers to recover heap
            if (tjErr && strstr(tjErr, "Insufficient memory")) {
                OSReport("[SC] Encode: JPEG OOM — cycling ring buffers to free memory\n");
                bool extracted = gRingBuffer.cycleAndSave();
                bool otherExtracted = gRingBufferTV.cycleAndSave();
                if (extracted || otherExtracted) {
                    OSReport("[SC] Encode: JPEG OOM — extracting audio snapshots\n");
                    gCycleAudioWritePosDRC = getAudioWritePos(AUDIO_SRC_DRC);
                    gCycleAudioWritePosTV  = getAudioWritePos(AUDIO_SRC_TV);
                    free(gPendingAudioDRC); gPendingAudioDRC = nullptr;
                    free(gPendingAudioTV);  gPendingAudioTV  = nullptr;
                    auto snap = [](AudioSource src, int16_t **out, size_t *num, uint32_t *rate) {
                        const int16_t *b = nullptr; size_t s = 0; uint32_t r = 0;
                        getAudioData(src, &b, &s, &r);
                        size_t bytes = s * sizeof(int16_t);
                        *out = (int16_t *) malloc(bytes);
                        if (*out) { memcpy(*out, b, bytes); *num = s; *rate = r; return true; }
                        else { *num = 0; *rate = 0; return false; }
                    };
                    snap(AUDIO_SRC_DRC, &gPendingAudioDRC, &gPendingAudioDRCSamps, &gPendingAudioDRCRate);
                    snap(AUDIO_SRC_TV,  &gPendingAudioTV,  &gPendingAudioTVSamps,  &gPendingAudioTVRate);
                    gUsePendingSave = true;
                    OSMemoryBarrier();
                    requestSave();
                }
                OSReport("[SC] Encode: JPEG OOM — emergencyCycle both buffers\n");
                gRingBuffer.emergencyCycle();
                gRingBufferTV.emergencyCycle();
                gRingBuffer.allocSlots();
                gRingBufferTV.allocSlots();
                NotificationModule_AddErrorNotification(
                    "\ue01e ScreenCapture: Low memory! Some frames dropped. Reduce buffer duration.");
            }

            free(cs);
            sProf.jpegUs += OSTicksToMicroseconds(OSGetTime() - tJpeg);
            sProf.encUs  += OSTicksToMicroseconds(OSGetTime() - tEncFrame);
            OSAddAtomic(&sEncodeInFlight, -1);
            tWait = OSGetTime(); continue;
        }

        uint8_t *copy = (uint8_t *) malloc(jpegSize);
        if (!copy) {
            RingBuffer &buf = cs->isTV ? gRingBufferTV : gRingBuffer;
            RingBuffer &other = cs->isTV ? gRingBuffer : gRingBufferTV;
            OSReport("[SC] OOM: MALLOC FAILED! size=%lu src=%s bufCount=%u otherCount=%u gSaveReq=%d\n",
                     jpegSize, cs->isTV ? "TV" : "DRC", buf.count(), other.count(), gSaveRequested);

            bool thisExtracted = false;
            bool otherExtracted = false;
            if (!gSaveRequested) {
                OSReport("[SC] OOM: gSaveReq=0, attempting cycleAndSave on both buffers\n");
                thisExtracted = buf.cycleAndSave();
                otherExtracted = other.cycleAndSave();
                OSReport("[SC] OOM: thisExtracted=%d otherExtracted=%d\n", thisExtracted, otherExtracted);
            } else {
                OSReport("[SC] OOM: gSaveReq=1, SKIPPING cycleAndSave (save in progress)\n");
            }
            if (thisExtracted || otherExtracted) {
                OSReport("[SC] OOM: extracting audio snapshots\n");
                gCycleAudioWritePosDRC = getAudioWritePos(AUDIO_SRC_DRC);
                gCycleAudioWritePosTV  = getAudioWritePos(AUDIO_SRC_TV);
                free(gPendingAudioDRC);
                free(gPendingAudioTV);
                auto snap = [](AudioSource src, int16_t **out, size_t *num, uint32_t *rate) {
                    const int16_t *b = nullptr; size_t s = 0; uint32_t r = 0;
                    getAudioData(src, &b, &s, &r);
                    size_t bytes = s * sizeof(int16_t);
                    *out = (int16_t *) malloc(bytes);
                    if (*out) { memcpy(*out, b, bytes); *num = s; *rate = r; return true; }
                    else { *num = 0; *rate = 0; return false; }
                };
                bool snapDrc = snap(AUDIO_SRC_DRC, &gPendingAudioDRC, &gPendingAudioDRCSamps, &gPendingAudioDRCRate);
                bool snapTv  = snap(AUDIO_SRC_TV,  &gPendingAudioTV,  &gPendingAudioTVSamps,  &gPendingAudioTVRate);
                OSReport("[SC] OOM: audio snapshots DRC=%d TV=%d\n", snapDrc, snapTv);
                if (snapDrc && snapTv) {
                    clearAudioBuffer();
                    OSReport("[SC] OOM: cleared audio buffer\n");
                }
                gUsePendingSave = true;
                OSMemoryBarrier();
                OSReport("[SC] OOM: calling requestSave()\n");
                NotificationModule_AddErrorNotification(
                    "\ue01e ScreenCapture: Out of memory! Saving partial buffer. Reduce resolution, buffer duration, or record quality in settings.");
                requestSave();
                OSReport("[SC] OOM: requestSave() returned\n");
            } else {
                OSReport("[SC] OOM: NOT extracted, freeing and notifying\n");
                NotificationModule_AddErrorNotification(
                    "\ue01e ScreenCapture: Out of memory! Could not save buffer. Reduce resolution, buffer duration, or record quality in settings.");
            }

            OSReport("[SC] OOM: emergencyCycle both buffers\n");
            buf.emergencyCycle();
            other.emergencyCycle();
            // Re-allocate slots so subsequent pushes can succeed
            buf.allocSlots();
            other.allocSlots();
            OSReport("[SC] OOM: after allocSlots DRC=%u TV=%u\n",
                     gRingBuffer.count(), gRingBufferTV.count());
            copy = (uint8_t *) malloc(jpegSize);
            if (!copy) {
                OSReport("[SC] OOM: retry malloc STILL FAILED, dropping frame\n");
            } else {
                OSReport("[SC] OOM: retry malloc OK (%lu bytes)\n", jpegSize);
            }
        }
        if (copy) {
            memcpy(copy, jpegBuf, jpegSize);
            RingBuffer &buf = cs->isTV ? gRingBufferTV : gRingBuffer;
            bool cycled = buf.push(copy, (size_t) jpegSize, cs->timestamp,
                                   cs->audioWritePos, w, h);

            uint32_t cnt = buf.count();
            OSReport("[SC] Encode: pushed to %s, cycled=%d, cnt=%u/%u\n",
                     cs->isTV ? "TV" : "DRC", cycled, cnt, gRingBufferFrameCount);

            if (cycled) {
                if (gSaveOnBufferEnd && !gSaveRequested) {
                    OSReport("[SC] Encode: CYCLE — starting auto-save\n");
                    gCycleAudioWritePosDRC = getAudioWritePos(AUDIO_SRC_DRC);
                    gCycleAudioWritePosTV  = getAudioWritePos(AUDIO_SRC_TV);
                    OSReport("[SC] Encode: audioWritePos DRC=%llu TV=%llu\n",
                             (unsigned long long)gCycleAudioWritePosDRC,
                             (unsigned long long)gCycleAudioWritePosTV);

                    auto snapshotAudio = [](AudioSource src,
                                            int16_t **outSamples,
                                            size_t *outNum,
                                            uint32_t *outRate) {
                        const int16_t *buf = nullptr;
                        size_t samps = 0;
                        uint32_t rate = 0;
                        getAudioData(src, &buf, &samps, &rate);
                        size_t bytes = samps * sizeof(int16_t);
                        *outSamples = (int16_t *) malloc(bytes);
                        if (*outSamples) {
                            memcpy(*outSamples, buf, bytes);
                            *outNum = samps;
                            *outRate = rate;
                            OSReport("[SC] Encode: audio snapshot src=%d OK samps=%u\n", (int)src, (uint32_t)samps);
                            return true;
                        } else {
                            OSReport("[SC] Encode: audio snapshot src=%d MALLOC FAILED!\n", (int)src);
                            *outNum = 0;
                            *outRate = 0;
                            return false;
                        }
                    };
                    free(gPendingAudioDRC);
                    free(gPendingAudioTV);
                    bool drcSnapOk = snapshotAudio(AUDIO_SRC_DRC,
                                                   &gPendingAudioDRC, &gPendingAudioDRCSamps, &gPendingAudioDRCRate);
                    bool tvSnapOk  = snapshotAudio(AUDIO_SRC_TV,
                                                   &gPendingAudioTV, &gPendingAudioTVSamps, &gPendingAudioTVRate);
                    OSReport("[SC] Encode: audio snapshots DRC=%d TV=%d\n", drcSnapOk, tvSnapOk);

                    if (drcSnapOk && tvSnapOk) {
                        clearAudioBuffer();
                        OSReport("[SC] Encode: cleared audio buffer\n");
                    }
                    gUsePendingSave = true;
                    OSMemoryBarrier();
                    OSReport("[SC] Encode: calling requestSave()\n");
                    requestSave();
                    OSReport("[SC] Encode: requestSave() returned, gSaveReq=%d\n", gSaveRequested);
                } else {
                    OSReport("[SC] Encode: CYCLE — skip save (saveOnEnd=%d gSaveReq=%d)\n",
                             gSaveOnBufferEnd, gSaveRequested);
                    if (gSaveOnBufferEnd && gSaveRequested) {
                        NotificationModule_AddInfoNotification(
                            "\ue01e ScreenCapture: Buffer full while saving! Some frames were lost. Try reducing buffer duration.");
                    }
                    clearAudioBuffer();
                    OSReport("[SC] Encode: cleared audio buffer after skip\n");
                }
            }
        }

        free(cs);
        sProf.jpegUs += OSTicksToMicroseconds(OSGetTime() - tJpeg);
        sProf.encUs  += OSTicksToMicroseconds(OSGetTime() - tEncFrame);
        sProf.encCount++;
        OSAddAtomic(&sEncodeInFlight, -1);

        tWait = OSGetTime();
    }

    if (jpegBuf) free(jpegBuf);
    tjDestroy(tj);
    return 0;
}

void startEncodeThread() {
    if (sEncodeRunning) return;

    constexpr uint32_t STACK_SIZE = 256 * 1024;

    sEncodeThread = (OSThread *) memalign(8, sizeof(OSThread));
    sEncodeStack  = (uint8_t *)  memalign(0x20, STACK_SIZE);
    if (!sEncodeThread || !sEncodeStack) {
        if (sEncodeThread) { free(sEncodeThread); sEncodeThread = nullptr; }
        if (sEncodeStack)  { free(sEncodeStack);  sEncodeStack  = nullptr; }
        return;
    }

    OSInitMessageQueue(&sEncodeQueue, sEncodeMessages,
                       sizeof(sEncodeMessages) / sizeof(sEncodeMessages[0]));

    if (!OSCreateThread(sEncodeThread, encodeThreadEntry,
                        1, (char *) &sEncodeQueue,
                        (void *)((uint32_t) sEncodeStack + STACK_SIZE),
                        STACK_SIZE, 20,
                        OS_THREAD_ATTRIB_AFFINITY_ANY)) {
        free(sEncodeThread); sEncodeThread = nullptr;
        free(sEncodeStack);  sEncodeStack  = nullptr;
        return;
    }

    OSSetThreadName(sEncodeThread, "ScreenCapture Encode Thread");
    OSResumeThread(sEncodeThread);
    sEncodeRunning = true;
}

void waitForEncodeDrain() {
    flushPendingSync();
    while (sEncodeInFlight > 0) {
        OSSleepTicks(OSMillisecondsToTicks(1));
    }
    OSSleepTicks(OSMillisecondsToTicks(10));
}

void stopEncodeThread(bool flushSync) {
    if (!sEncodeRunning) return;

    if (flushSync) flushPendingSync();

    OSMessage msg;
    msg.message = ENCODE_CMD_STOP;
    OSSendMessage(&sEncodeQueue, &msg, OS_MESSAGE_FLAGS_BLOCKING);

    OSSetThreadPriority(sEncodeThread, 0);
    OSJoinThread(sEncodeThread, nullptr);

    free(sEncodeThread); sEncodeThread = nullptr;
    free(sEncodeStack);  sEncodeStack  = nullptr;
    sEncodeRunning = false;

    destroySurfacePool();
}

void captureFrame(GX2ColorBuffer *srcBuffer, GX2SurfaceFormat srcFormat, bool isTV) {
    (void)srcFormat;
    if (!srcBuffer) return;

    OSTime tCap = OSGetTime();

    if (!sPoolInited) {
        initSurfacePool();
        if (!sPoolInited) {
            OSReport("[ScreenCapture] captureFrame: pool not inited\n");
            return;
        }
    }

    int srcIdx = isTV ? 1 : 0;

    { static uint32_t throttle = 0; if ((++throttle % 10) == 1)
        OSReport("[ScreenCapture] captureFrame: src=%s encInFlight=%d\n",
                 isTV ? "TV" : "DRC", sEncodeInFlight); }
    syncAndSend(srcIdx);

    int32_t otherPending = (srcIdx == 0) ? sPendingSyncIdx[1] : sPendingSyncIdx[0];
    uint32_t idx = SURFACE_POOL_SIZE;
    for (uint32_t i = 0; i < SURFACE_POOL_SIZE; i++) {
        uint32_t candidate = (sPoolWriteIdx + i) % SURFACE_POOL_SIZE;
        if (!sPool[candidate].busy && (int32_t)candidate != otherPending) {
            idx = candidate;
            break;
        }
    }

    if (idx >= SURFACE_POOL_SIZE) {
        OSReport("[ScreenCapture] captureFrame: NO FREE SURFACE! pendingSync=[%d,%d] encInFlight=%d\n",
                 sPendingSyncIdx[0], sPendingSyncIdx[1], sEncodeInFlight);
        for (uint32_t i = 0; i < SURFACE_POOL_SIZE; i++) {
            OSReport("  pool[%u]: busy=%d\n", i, sPool[i].busy);
        }
        sProf.frameCount++;
        return;
    }

    PoolSurface *ps = &sPool[idx];

    issueAsyncCopy(srcBuffer, ps);

    ps->needsSRGB     = (srcFormat & 0x400) != 0;
    ps->audioWritePos = isTV ? getAudioWritePos(AUDIO_SRC_TV) : getAudioWritePos(AUDIO_SRC_DRC);
    ps->gpuTimestamp  = GX2GetLastSubmittedTimeStamp();
    GX2Flush();

    sPendingSyncIdx[srcIdx] = (int32_t)idx;
    sPoolWriteIdx = (idx + 1) % SURFACE_POOL_SIZE;

    sProf.captureUs += OSTicksToMicroseconds(OSGetTime() - tCap);
    if (++sProf.frameCount >= PROF_LOG_INTERVAL) {
        profLog();
    }
}
