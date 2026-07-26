#include "save_thread.h"
#include "common.h"
#include "retain_vars.hpp"
#include "ring_buffer.h"
#include "avi_writer.h"
#include "capture_utils.h"
#include "audio_capture.h"
#include "fs/FSUtils.h"
#include "utils/logger.h"
#include "utils/StringTools.h"
#include <notifications/notifications.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/messagequeue.h>
#include <coreinit/cache.h>
#include <malloc.h>
#include <string.h>
#include <dirent.h>
#include <exception>

static OSThread       *sSaveThread  = nullptr;
static uint8_t        *sSaveStack   = nullptr;
static OSMessageQueue  sSaveQueue;
static OSMessage       sSaveMessages[4];
static bool            sThreadSetup = false;

#define CMD_STOP  0xDEAD0001u
#define CMD_SAVE  0xDEAD0002u

static int32_t saveThreadEntry([[maybe_unused]] int argc, const char **argv) {
    auto *queue = (OSMessageQueue *) argv;

    OSMessage msg;
    while (OSReceiveMessage(queue, &msg, OS_MESSAGE_FLAGS_BLOCKING)) {
        uint32_t cmd = (uint32_t)(uintptr_t) msg.message;

        if (cmd == CMD_STOP) {
            DEBUG_FUNCTION_LINE("saveThread: received STOP\n");
            break;
        }

        if (cmd == CMD_SAVE) {
            OSReport("[SC] saveThread: CMD_SAVE received (gUsePendingSave=%d)\n", gUsePendingSave);

            gCapturing = false;
            OSMemoryBarrier();
            OSReport("[SC] saveThread: gCapturing=0\n");

            stopAudioCapture();
            waitForEncodeDrain();
            OSReport("[SC] saveThread: encode drained\n");

            const int16_t *drcBuf   = nullptr;
            size_t         drcSamps = 0;
            uint32_t       drcRate  = 48000;
            const int16_t *tvBuf    = nullptr;
            size_t         tvSamps  = 0;
            uint32_t       tvRate   = 48000;

            // Shared helper: read live audio buffer, trim to oldest frame's writePos
            auto prepareAudio = [](AudioSource src, RingBuffer &rb,
                                   const int16_t **outBuf, size_t *outSamps,
                                   uint32_t *outRate) {
                const int16_t *rawBuf   = nullptr;
                size_t         rawSamps = 0;
                uint32_t       rate     = 48000;
                getAudioData(src, &rawBuf, &rawSamps, &rate);

                const CapturedFrame *oldest = rb.get(0);
                size_t skipBytes = oldest ? (size_t)oldest->audioWritePos : 0;
                size_t skipSamp  = skipBytes / sizeof(int16_t);
                if (skipSamp >= rawSamps) skipSamp = 0;

                *outBuf   = rawBuf   + skipSamp;
                *outSamps = rawSamps - skipSamp;
                *outRate  = rate;
                OSReport("[SC] saveThread: prepareAudio src=%d rawSamps=%u skip=%u outSamps=%u ringCount=%u\n",
                         (int)src, (uint32_t)rawSamps, (uint32_t)skipSamp, (uint32_t)*outSamps, rb.count());
            };

            PendingSave drcPS, tvPS;
            bool drcSlotsOk = true, tvSlotsOk = true;

            if (gUsePendingSave) {
                OSReport("[SC] saveThread: PENDING SAVE path\n");
                gUsePendingSave = false;
                OSMemoryBarrier();

                drcBuf   = gPendingAudioDRC;
                drcSamps = gPendingAudioDRCSamps;
                drcRate  = gPendingAudioDRCRate ? gPendingAudioDRCRate : 48000;
                tvBuf    = gPendingAudioTV;
                tvSamps  = gPendingAudioTVSamps;
                tvRate   = gPendingAudioTVRate ? gPendingAudioTVRate : 48000;
                OSReport("[SC] saveThread: pending audio DRC=%p(%u) TV=%p(%u)\n",
                         (void*)drcBuf, (uint32_t)drcSamps, (void*)tvBuf, (uint32_t)tvSamps);

                // Restore extracted pending frames back into ring buffers
                // so writeAVI can read them as usual
                auto restoreFrames = [](RingBuffer &rb, PendingSave &ps, const char *name) {
                    if (!ps.frames) {
                        OSReport("[SC] saveThread: no pending frames for %s\n", name);
                        return;
                    }
                    OSReport("[SC] saveThread: restoring %u frames into %s\n", ps.count, name);
                    uint32_t before = rb.count();
                    for (uint32_t i = 0; i < ps.count; i++) {
                        rb.push(ps.frames[i].data, ps.frames[i].size,
                                ps.frames[i].timestamp, ps.frames[i].audioWritePos,
                                ps.frames[i].width, ps.frames[i].height);
                        ps.frames[i].data = nullptr;
                    }
                    free(ps.frames);
                    ps.frames = nullptr;
                    ps.count  = 0;
                    OSReport("[SC] saveThread: restore %s done, before=%u after=%u\n", name, before, rb.count());
                };

                gRingBuffer.takePendingSave(drcPS);
                gRingBufferTV.takePendingSave(tvPS);
                OSReport("[SC] saveThread: takePending DRC=%u TV=%u\n", drcPS.count, tvPS.count);

                OSReport("[SC] saveThread: clearing live buffers before restore\n");
                gRingBuffer.clear();
                gRingBufferTV.clear();

                OSReport("[SC] saveThread: ensuring slots allocated before restore\n");
                drcSlotsOk = gRingBuffer.allocSlots();
                tvSlotsOk = gRingBufferTV.allocSlots();
                OSReport("[SC] saveThread: allocSlots DRC=%d TV=%d\n", drcSlotsOk, tvSlotsOk);

                OSReport("[SC] saveThread: restoring pending frames (DRC slots=%d TV slots=%d)\n",
                         drcSlotsOk, tvSlotsOk);
                if (drcSlotsOk) {
                    restoreFrames(gRingBuffer, drcPS, "DRC");
                } else {
                    OSReport("[SC] saveThread: DRC slots OOM, will write %u frames directly\n", drcPS.count);
                }
                if (tvSlotsOk) {
                    restoreFrames(gRingBufferTV, tvPS, "TV");
                } else {
                    OSReport("[SC] saveThread: TV slots OOM, will write %u frames directly\n", tvPS.count);
                }
                OSReport("[SC] saveThread: after restore DRC=%u TV=%u\n",
                         gRingBuffer.count(), gRingBufferTV.count());

                // If snapshot failed (NULL), fall back to reading from live buffer
                // (only if ring buffer has slots, otherwise get(0)
                if (drcBuf == nullptr) {
                    if (drcSlotsOk) {
                        OSReport("[SC] saveThread: DRC audio NULL, falling back to live\n");
                        prepareAudio(AUDIO_SRC_DRC, gRingBuffer,
                                     &drcBuf, &drcSamps, &drcRate);
                    } else {
                        OSReport("[SC] saveThread: DRC audio NULL, no slots — no audio\n");
                    }
                }
                if (tvBuf == nullptr) {
                    if (tvSlotsOk) {
                        OSReport("[SC] saveThread: TV audio NULL, falling back to live\n");
                        prepareAudio(AUDIO_SRC_TV,  gRingBufferTV,
                                     &tvBuf,  &tvSamps,  &tvRate);
                    } else {
                        OSReport("[SC] saveThread: TV audio NULL, no slots — no audio\n");
                    }
                }

            } else {
                OSReport("[SC] saveThread: NORMAL save path (not pending)\n");
                prepareAudio(AUDIO_SRC_DRC, gRingBuffer,
                             &drcBuf, &drcSamps, &drcRate);
                prepareAudio(AUDIO_SRC_TV,  gRingBufferTV,
                             &tvBuf,  &tvSamps,  &tvRate);
            }

            OSReport("[SC] saveThread: frames to write — DRC=%u TV=%u audio DRC=%u TV=%u\n",
                     gRingBuffer.count(), gRingBufferTV.count(),
                     (uint32_t)drcSamps, (uint32_t)tvSamps);

		NotificationModule_AddInfoNotification(
	    "\ue01e ScreenCapture: Saving video to SD card...");

            FSUtils::CreateSubfolder(WIIU_VIDEO_PATH);
            OSCalendarTime t;
            OSTicksToCalendarTime(OSGetTime(), &t);
            std::string dateStr = string_format(
                "%04d-%02d-%02d",
                t.tm_year, t.tm_mon + 1, t.tm_mday);
            std::string titleDir = string_format(
                "%s%016llX (%s)",
                WIIU_VIDEO_PATH, gTitleID, gShortNameEn.c_str());
            std::string dateDir = string_format(
                "%s/%s",
                titleDir.c_str(), dateStr.c_str());
            FSUtils::CreateSubfolder(dateDir.c_str());
            std::string base = string_format(
                "%s/%s_%02d.%02d.%02d",
                dateDir.c_str(), dateStr.c_str(),
                t.tm_hour, t.tm_min, t.tm_sec);

            bool ok = false;

            OSReport("[SC] saveThread: source=%d write DRC=%u TV=%u audio DRC=%u TV=%u\n",
                     gCaptureSource, gRingBuffer.count(), gRingBufferTV.count(),
                     (uint32_t)drcSamps, (uint32_t)tvSamps);

            try {
                if (gCaptureSource == CAPTURE_SOURCE_TV) {
                    if (tvSlotsOk) {
                        ok = writeAVI(base + "_TV.avi", gRingBufferTV,
                                      tvBuf, tvSamps, tvRate);
                    } else if (tvPS.frames) {
                        OSReport("[SC] saveThread: writing TV from %u pending frames\n", tvPS.count);
                        ok = writeAVI(base + "_TV.avi",
                                      tvPS.frames, tvPS.count,
                                      tvBuf, tvSamps, tvRate);
                    }
                } else if (gCaptureSource == CAPTURE_SOURCE_BOTH) {
                    bool okDRC = drcSlotsOk
                        ? writeAVI(base + "_DRC.avi", gRingBuffer,
                                   drcBuf, drcSamps, drcRate)
                        : (drcPS.frames
                            ? writeAVI(base + "_DRC.avi",
                                       drcPS.frames, drcPS.count,
                                       drcBuf, drcSamps, drcRate)
                            : false);
                    bool okTV  = tvSlotsOk
                        ? writeAVI(base + "_TV.avi",  gRingBufferTV,
                                   tvBuf,  tvSamps,  tvRate)
                        : (tvPS.frames
                            ? writeAVI(base + "_TV.avi",
                                       tvPS.frames, tvPS.count,
                                       tvBuf,  tvSamps,  tvRate)
                            : false);
                    ok = okDRC || okTV;
                } else {
                    if (drcSlotsOk) {
                        ok = writeAVI(base + "_DRC.avi", gRingBuffer,
                                      drcBuf, drcSamps, drcRate);
                    } else if (drcPS.frames) {
                        OSReport("[SC] saveThread: writing DRC from %u pending frames\n", drcPS.count);
                        ok = writeAVI(base + "_DRC.avi",
                                      drcPS.frames, drcPS.count,
                                      drcBuf, drcSamps, drcRate);
                    }
                }
            } catch (std::exception &ex) {
                OSReport("[SC] saveThread: writeAVI exception: %s\n", ex.what());
                ok = false;
            } catch (...) {
                OSReport("[SC] saveThread: writeAVI unknown exception\n");
                ok = false;
            }

            OSReport("[SC] saveThread: writeAVI returned %d\n", ok);
            if (ok) {
		NotificationModule_AddInfoNotification(
	    "\ue01e ScreenCapture: Video saved!");
            } else {
		NotificationModule_AddErrorNotification(
	    "\ue01e ScreenCapture: Save FAILED! Out of memory.");
            }

            OSReport("[SC] saveThread: cleanup — freeing pending audio/frames, clearing buffers\n");
            free(gPendingAudioDRC);
            free(gPendingAudioTV);
            gPendingAudioDRC       = nullptr;
            gPendingAudioTV        = nullptr;
            gPendingAudioDRCSamps  = 0;
            gPendingAudioTVSamps   = 0;
            gPendingAudioDRCRate   = 0;
            gPendingAudioTVRate    = 0;

            // Free any un-restored pending frames (direct-write path consumes data)
            if (drcPS.frames) {
                for (uint32_t i = 0; i < drcPS.count; i++)
                    free(drcPS.frames[i].data);
                free(drcPS.frames);
                drcPS.frames = nullptr; drcPS.count = 0;
            }
            if (tvPS.frames) {
                for (uint32_t i = 0; i < tvPS.count; i++)
                    free(tvPS.frames[i].data);
                free(tvPS.frames);
                tvPS.frames = nullptr; tvPS.count = 0;
            }

            gRingBuffer.clear();
            gRingBufferTV.clear();
            OSReport("[SC] saveThread: after clear DRC=%u TV=%u\n",
                     gRingBuffer.count(), gRingBufferTV.count());
            clearAudioBuffer();
            gSaveRequested = false;
            OSReport("[SC] saveThread: gSaveRequested=0\n");

            if (gStopOnAutoSave) {
                OSReport("[ScreenCapture] saveThread: auto-stop after save\n");
                gEnabled = false;
                stopEncodeThread(false);
                destroyAudioCapture();
                gRingBuffer.freeSlots();
                gRingBufferTV.freeSlots();
                NotificationModule_AddInfoNotification(
                    "\ue01e ScreenCapture: Recording stopped.");
            } else {
                initAudioCapture();
                gCapturing     = true;
                OSMemoryBarrier();
            }
        }
    }

    return 0;
}

void startSaveThread() {
    if (sThreadSetup) return;

    constexpr uint32_t STACK_SIZE = 64 * 1024;

    sSaveThread = (OSThread *) memalign(8, sizeof(OSThread));
    sSaveStack  = (uint8_t *)  memalign(0x20, STACK_SIZE);

    if (!sSaveThread || !sSaveStack) {
        DEBUG_FUNCTION_LINE("saveThread: allocation failed\n");
        if (sSaveThread) { free(sSaveThread); sSaveThread = nullptr; }
        if (sSaveStack)  { free(sSaveStack);  sSaveStack  = nullptr; }
        return;
    }

    OSInitMessageQueue(&sSaveQueue, sSaveMessages,
                       sizeof(sSaveMessages) / sizeof(sSaveMessages[0]));

    if (!OSCreateThread(sSaveThread,
                        saveThreadEntry,
                        1, (char *) &sSaveQueue,
                        (void *)((uint32_t) sSaveStack + STACK_SIZE),
                        STACK_SIZE,
                        16,
                        OS_THREAD_ATTRIB_AFFINITY_ANY)) {
        DEBUG_FUNCTION_LINE("saveThread: OSCreateThread failed\n");
        free(sSaveThread); sSaveThread = nullptr;
        free(sSaveStack);  sSaveStack  = nullptr;
        return;
    }

    OSSetThreadName(sSaveThread, "ScreenCapture Save Thread");
    OSResumeThread(sSaveThread);
    sThreadSetup = true;
    OSMemoryBarrier();
}

void stopSaveThread() {
    if (!sThreadSetup) return;

    OSMessage msg;
    msg.message = (void *)(uintptr_t) CMD_STOP;
    OSSendMessage(&sSaveQueue, &msg, OS_MESSAGE_FLAGS_BLOCKING);

    OSSetThreadPriority(sSaveThread, 0);
    OSJoinThread(sSaveThread, nullptr);

    free(sSaveThread); sSaveThread = nullptr;
    free(sSaveStack);  sSaveStack  = nullptr;
    sThreadSetup = false;
}

void requestSave() {
    OSReport("[SC] requestSave: threadSetup=%d gSaveRequested=%d\n",
             sThreadSetup, gSaveRequested);
    if (!sThreadSetup) {
        OSReport("[SC] requestSave: FAILED — no save thread\n");
        return;
    }
    if (gSaveRequested) {
        OSReport("[SC] requestSave: FAILED — already requested\n");
        return;
    }

    OSReport("[SC] requestSave: setting gSaveRequested=1 and sending CMD_SAVE\n");
    gSaveRequested = true;
    OSMemoryBarrier();

    OSMessage msg;
    msg.message = (void *)(uintptr_t) CMD_SAVE;
    if (!OSSendMessage(&sSaveQueue, &msg, OS_MESSAGE_FLAGS_NONE)) {
        OSReport("[SC] requestSave: CMD_SAVE queue FULL!\n");
    }
    OSReport("[SC] requestSave: CMD_SAVE sent\n");
}
