#include "audio_capture.h"
#include "common.h"
#include "retain_vars.hpp"
#include <coreinit/debug.h>
#include <coreinit/memory.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/cache.h>

#define AUDIO_LOG(FMT, ARGS...) OSReport("[AudioCapture] " FMT "\n", ##ARGS)

struct AudioBuf {
    int16_t        *buf     = nullptr;
    size_t          capacity = 0;
    volatile size_t writePos = 0;
};

static AudioBuf sAudio[2];

static AudioBuf *bufFor(AudioSource src) {
    return (src == AUDIO_SRC_TV) ? &sAudio[0] : &sAudio[1];
}

void initAudioCapture() {
    AUDIO_LOG("initAudioCapture()\n");
    uint32_t secs   = (uint32_t) gRingBufferSeconds;
    if (secs < 30) secs = 30;
    size_t bufSize  = (size_t) 48000 * 4 * secs;

    for (int i = 0; i < 2; i++) {
        AudioBuf *ab = &sAudio[i];
        if (!ab->buf || ab->capacity < bufSize) {
            if (ab->buf) { MEMFreeToDefaultHeap(ab->buf); ab->buf = nullptr; }
            ab->buf = (int16_t *) MEMAllocFromDefaultHeapEx(bufSize, 0x40);
            if (!ab->buf) {
                AUDIO_LOG("source %d ALLOCATION FAILED (%u bytes)!\n", i, (uint32_t)bufSize);
                ab->capacity = 0;
                continue;
            }
            memset(ab->buf, 0, bufSize);
            ab->capacity = bufSize;
            AUDIO_LOG("source %d allocated %u bytes at %p\n", i, (uint32_t)bufSize, ab->buf);
        }
        ab->writePos = 0;
    }
}

void captureAudioBuffer(AudioSource src, const void *buffer, uint32_t size) {
    AudioBuf *ab = bufFor(src);
    if (!ab->buf || !buffer || size == 0) {
        { static uint32_t t = 0; if ((++t % 60) == 1) AUDIO_LOG("DROP src=%d: no buf/buf=%p/size=%u\n", (int)src, (void*)ab->buf, size); }
        return;
    }
    if (size > 64 * 1024) {
        { static uint32_t t = 0; if ((++t % 60) == 1) AUDIO_LOG("DROP src=%d: size %u > 64K\n", (int)src, size); }
        return;
    }

    size_t w = ab->writePos;
    if (w + size > ab->capacity) {
        { static uint32_t t = 0; if ((++t % 60) == 1) AUDIO_LOG("DROP src=%d: w=%u + size=%u > cap=%u\n", (int)src, (uint32_t)w, size, (uint32_t)ab->capacity); }
        return;
    }

    { static uint32_t t = 0; if ((++t % 60) == 1) AUDIO_LOG("CAPTURE src=%d size=%u w=%u cap=%u\n", (int)src, size, (uint32_t)w, (uint32_t)ab->capacity); }

    memcpy((uint8_t *)ab->buf + w, buffer, size);
    OSMemoryBarrier();
    ab->writePos = w + size;
}

void stopAudioCapture() {
    OSMemoryBarrier();
}

void getAudioData(AudioSource src, const int16_t **data, size_t *numSamples, uint32_t *sampleRate) {
    AudioBuf *ab = bufFor(src);
    size_t bytes = ab->writePos;
    if (data)       *data       = ab->buf;
    if (numSamples) *numSamples = bytes / sizeof(int16_t);
    if (sampleRate) *sampleRate = 48000;
    AUDIO_LOG("getAudioData(src=%d): %u bytes, %u samples\n",
              (int)src, (uint32_t)bytes, (uint32_t)(bytes / sizeof(int16_t)));
}

uint64_t getAudioWritePos(AudioSource src) {
    OSMemoryBarrier();
    return (uint64_t)bufFor(src)->writePos;
}

void clearAudioBuffer() {
    for (int i = 0; i < 2; i++) {
        AudioBuf *ab = &sAudio[i];
        ab->writePos = 0;
        if (ab->buf) memset(ab->buf, 0, ab->capacity);
    }
}

void destroyAudioCapture() {
    for (int i = 0; i < 2; i++) {
        AudioBuf *ab = &sAudio[i];
        ab->capacity = 0;
        ab->writePos = 0;
        if (ab->buf) { MEMFreeToDefaultHeap(ab->buf); ab->buf = nullptr; }
    }
}
