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
            DEBUG_FUNCTION_LINE("saveThread: starting save\n");

            gCapturing = false;
            OSMemoryBarrier();

            // Stop audio capture and drain any in-flight audio
            stopAudioCapture();
            waitForEncodeDrain();

            // grabs the audio for one source and trim it to match
            // the oldest video frame's capture time.
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
            };

            const int16_t *drcBuf   = nullptr;
            size_t         drcSamps = 0;
            uint32_t       drcRate  = 48000;
            const int16_t *tvBuf    = nullptr;
            size_t         tvSamps  = 0;
            uint32_t       tvRate   = 48000;

            prepareAudio(AUDIO_SRC_DRC, gRingBuffer,
                         &drcBuf, &drcSamps, &drcRate);
            prepareAudio(AUDIO_SRC_TV,  gRingBufferTV,
                         &tvBuf,  &tvSamps,  &tvRate);

		NotificationModule_AddInfoNotification(
	    "\ue01e ScreenCapture: Saving video to SD card...");

            FSUtils::CreateSubfolder(WIIU_VIDEO_PATH);
            OSCalendarTime t;
            OSTicksToCalendarTime(OSGetTime(), &t);
            std::string dateStr = string_format(
                "%04d-%02d-%02d",
                t.tm_year, t.tm_mon + 1, t.tm_mday);
            std::string dateDir = string_format(
                "%s%s/%s",
                WIIU_VIDEO_PATH, gShortNameEn.c_str(), dateStr.c_str());
            FSUtils::CreateSubfolder(dateDir.c_str());
            std::string base = string_format(
                "%s/%s_%02d.%02d.%02d",
                dateDir.c_str(), dateStr.c_str(),
                t.tm_hour, t.tm_min, t.tm_sec);

            bool ok = false;

            try {
                if (gCaptureSource == CAPTURE_SOURCE_TV) {
                    ok = writeAVI(base + "_TV.avi", gRingBufferTV,
                                  tvBuf, tvSamps, tvRate);
                } else if (gCaptureSource == CAPTURE_SOURCE_BOTH) {
                    bool okDRC = writeAVI(base + "_DRC.avi", gRingBuffer,
                                          drcBuf, drcSamps, drcRate);
                    bool okTV  = writeAVI(base + "_TV.avi",  gRingBufferTV,
                                          tvBuf,  tvSamps,  tvRate);
                    ok = okDRC || okTV;
                } else {
                    ok = writeAVI(base + "_DRC.avi", gRingBuffer,
                                  drcBuf, drcSamps, drcRate);
                }
            } catch (std::exception &ex) {
                DEBUG_FUNCTION_LINE("saveThread: writeAVI exception: %s\n", ex.what());
                ok = false;
            } catch (...) {
                DEBUG_FUNCTION_LINE("saveThread: writeAVI unknown exception\n");
                ok = false;
            }

            if (ok) {
		NotificationModule_AddInfoNotification(
	    "\ue01e ScreenCapture: Video saved!");
            } else {
		NotificationModule_AddErrorNotification(
	    "\ue01e ScreenCapture: Save FAILED! Out of memory.");
            }

            gRingBuffer.clear();
            gRingBufferTV.clear();
            clearAudioBuffer();
            gSaveRequested = false;

            initAudioCapture();
            gCapturing     = true;
            OSMemoryBarrier();
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
    if (!sThreadSetup) return;
    if (gSaveRequested) return;

    gSaveRequested = true;
    OSMemoryBarrier();

    OSMessage msg;
    msg.message = (void *)(uintptr_t) CMD_SAVE;
    OSSendMessage(&sSaveQueue, &msg, OS_MESSAGE_FLAGS_NONE);
}
