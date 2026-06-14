#pragma once

#include "common.h"
#include <stdint.h>
#include <stddef.h>
#include <coreinit/mutex.h>

// A single captured JPEG frame stored in RAM.
struct CapturedFrame {
    uint8_t *data;
    size_t   size;
    uint64_t timestamp;
    uint64_t audioWritePos;
    uint32_t width;
    uint32_t height;
};

// Thread-safe ring buffer of CapturedFrame entries. When full, the oldest frame is overwritten (freed first)
class RingBuffer {
public:
    RingBuffer();
    ~RingBuffer();

    bool push(uint8_t *jpegData, size_t jpegSize, uint64_t timestamp, uint64_t audioWritePos, uint32_t width, uint32_t height);

    // Number of frames currently stored.
    uint32_t count() const { return mCount; }

    const CapturedFrame *get(uint32_t index) const;

    void clear();

    void lock();
    void unlock();

    void freeSlots();

    bool allocSlots();

private:
    // Capacity driven by gRingBufferFrameCount (set from config menu)
    static constexpr uint32_t CAPACITY = RING_BUFFER_FRAMES_MAX;

    CapturedFrame *mSlots;
    uint32_t       mHead;
    uint32_t       mCount;
    OSMutex        mMutex;
};

extern RingBuffer gRingBuffer;
extern RingBuffer gRingBufferTV;
