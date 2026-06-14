#include "ring_buffer.h"
#include "retain_vars.hpp"
#include <malloc.h>
#include <string.h>

RingBuffer gRingBuffer;

RingBuffer::RingBuffer()
    : mHead(0), mCount(0) {
    mSlots = (CapturedFrame *) memalign(4, CAPACITY * sizeof(CapturedFrame));
    if (mSlots) {
        memset(mSlots, 0, CAPACITY * sizeof(CapturedFrame));
    }
    OSInitMutex(&mMutex);
}

RingBuffer::~RingBuffer() {
    clear();
    if (mSlots) {
        free(mSlots);
        mSlots = nullptr;
    }
}

bool RingBuffer::push(uint8_t *jpegData, size_t jpegSize, uint64_t timestamp, uint64_t audioWritePos, uint32_t width, uint32_t height) {
    if (!mSlots) return false;

    OSLockMutex(&mMutex);

    bool startedNewCycle = false;
    if (gRingBufferFrameCount > 0 && mCount >= gRingBufferFrameCount) {
        // Buffer reached configured duration, free all frames and restart
        for (uint32_t i = 0; i < CAPACITY; i++) {
            if (mSlots[i].data) {
                free(mSlots[i].data);
                mSlots[i].data = nullptr;
            }
        }
        mHead  = 0;
        mCount = 0;
        startedNewCycle = true;
    }

    uint32_t writeIdx;
    if (mCount < CAPACITY) {
        writeIdx = (mHead + mCount) % CAPACITY;
        mCount++;
    } else {
        writeIdx = mHead;
        if (mSlots[writeIdx].data) {
            free(mSlots[writeIdx].data);
            mSlots[writeIdx].data = nullptr;
        }
        mHead = (mHead + 1) % CAPACITY;
    }

    mSlots[writeIdx].data          = jpegData;
    mSlots[writeIdx].size          = jpegSize;
    mSlots[writeIdx].timestamp     = timestamp;
    mSlots[writeIdx].audioWritePos = audioWritePos;
    mSlots[writeIdx].width         = width;
    mSlots[writeIdx].height        = height;

    OSUnlockMutex(&mMutex);
    return startedNewCycle;
}

const CapturedFrame *RingBuffer::get(uint32_t index) const {
    if (!mSlots || index >= mCount) return nullptr;
    return &mSlots[(mHead + index) % CAPACITY];
}

void RingBuffer::clear() {
    if (!mSlots) return;
    OSLockMutex(&mMutex);
    for (uint32_t i = 0; i < CAPACITY; i++) {
        if (mSlots[i].data) {
            free(mSlots[i].data);
            mSlots[i].data = nullptr;
            mSlots[i].size = 0;
        }
    }
    mHead  = 0;
    mCount = 0;
    OSUnlockMutex(&mMutex);
}

void RingBuffer::lock()   { OSLockMutex(&mMutex); }
void RingBuffer::unlock() { OSUnlockMutex(&mMutex); }

void RingBuffer::freeSlots() {
    OSLockMutex(&mMutex);
    if (mSlots) {
        for (uint32_t i = 0; i < CAPACITY; i++) {
            if (mSlots[i].data) {
                free(mSlots[i].data);
                mSlots[i].data = nullptr;
            }
        }
        free(mSlots);
        mSlots = nullptr;
    }
    mHead  = 0;
    mCount = 0;
    OSUnlockMutex(&mMutex);
}

bool RingBuffer::allocSlots() {
    OSLockMutex(&mMutex);
    if (!mSlots) {
        mSlots = (CapturedFrame *) memalign(4, CAPACITY * sizeof(CapturedFrame));
        if (mSlots) {
            memset(mSlots, 0, CAPACITY * sizeof(CapturedFrame));
        }
    }
    OSUnlockMutex(&mMutex);
    return mSlots != nullptr;
}