#include "ring_buffer.h"
#include "retain_vars.hpp"
#include "utils/logger.h"
#include <coreinit/memdefaultheap.h>
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
        startedNewCycle = true;
        OSReport("[SC] push: CYCLE at count=%u/%u, saveOnEnd=%d, pending=%p, gSaveReq=%d\n",
                 mCount, gRingBufferFrameCount, gSaveOnBufferEnd, (void*)mPendingSave.frames, gSaveRequested);

        if (gSaveOnBufferEnd && !mPendingSave.frames) {
            OSReport("[SC] push: EXTRACTING %u frames to pending save\n", mCount);
            mPendingSave.frames = mSlots;
            mPendingSave.count  = mCount;

            mSlots = (CapturedFrame *) memalign(4, CAPACITY * sizeof(CapturedFrame));
            if (mSlots) {
                memset(mSlots, 0, CAPACITY * sizeof(CapturedFrame));
                OSReport("[SC] push: allocated fresh slots OK\n");
            } else {
                OSReport("[SC] push: FAILED to alloc fresh slots, freeing pending!\n");
                for (uint32_t i = 0; i < mPendingSave.count; i++) {
                    if (mPendingSave.frames[i].data) {
                        free(mPendingSave.frames[i].data);
                    }
                }
                free(mPendingSave.frames);
                mPendingSave.frames = nullptr;
                mPendingSave.count  = 0;

                mSlots = (CapturedFrame *) memalign(4, CAPACITY * sizeof(CapturedFrame));
                if (mSlots) memset(mSlots, 0, CAPACITY * sizeof(CapturedFrame));
            }
        } else {
            OSReport("[SC] push: FREEING %u frames (reason: saveOnEnd=%d pendingBusy=%d)\n",
                     mCount, gSaveOnBufferEnd, mPendingSave.frames != nullptr);
            for (uint32_t i = 0; i < CAPACITY; i++) {
                if (mSlots[i].data) {
                    free(mSlots[i].data);
                    mSlots[i].data = nullptr;
                }
            }
        }

        mHead  = 0;
        mCount = 0;
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
    OSLockMutex(&mMutex);
    OSReport("[SC] clear: mCount=%u mHead=%u pending=%p pendingCount=%u\n",
             mCount, mHead, (void*)mPendingSave.frames, mPendingSave.count);
    if (mSlots) {
        for (uint32_t i = 0; i < CAPACITY; i++) {
            if (mSlots[i].data) {
                free(mSlots[i].data);
                mSlots[i].data = nullptr;
                mSlots[i].size = 0;
            }
        }
    }
    if (mPendingSave.frames) {
        OSReport("[SC] clear: FREEING orphaned pending save with %u frames!\n", mPendingSave.count);
        for (uint32_t i = 0; i < mPendingSave.count; i++) {
            if (mPendingSave.frames[i].data) {
                free(mPendingSave.frames[i].data);
            }
        }
        free(mPendingSave.frames);
        mPendingSave.frames = nullptr;
        mPendingSave.count  = 0;
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
    if (mPendingSave.frames) {
        for (uint32_t i = 0; i < mPendingSave.count; i++) {
            if (mPendingSave.frames[i].data) {
                free(mPendingSave.frames[i].data);
            }
        }
        free(mPendingSave.frames);
        mPendingSave.frames = nullptr;
        mPendingSave.count  = 0;
    }
    mHead  = 0;
    mCount = 0;
    OSUnlockMutex(&mMutex);
}

bool RingBuffer::takePendingSave(PendingSave &out) {
    OSLockMutex(&mMutex);
    if (!mPendingSave.frames) {
        OSUnlockMutex(&mMutex);
        OSReport("[SC] takePendingSave: NO pending save\n");
        return false;
    }
    out.frames = mPendingSave.frames;
    out.count  = mPendingSave.count;
    OSReport("[SC] takePendingSave: took %u frames\n", out.count);
    mPendingSave.frames = nullptr;
    mPendingSave.count  = 0;
    OSUnlockMutex(&mMutex);
    return true;
}

void RingBuffer::freePendingSave() {
    OSLockMutex(&mMutex);
    if (mPendingSave.frames) {
        for (uint32_t i = 0; i < mPendingSave.count; i++) {
            if (mPendingSave.frames[i].data) {
                free(mPendingSave.frames[i].data);
            }
        }
        free(mPendingSave.frames);
        mPendingSave.frames = nullptr;
        mPendingSave.count  = 0;
    }
    OSUnlockMutex(&mMutex);
}

bool RingBuffer::cycleAndSave() {
    OSLockMutex(&mMutex);
    if (mCount == 0 || mPendingSave.frames) {
        OSReport("[SC] cycleAndSave: FAIL (count=%u pending=%p)\n", mCount, (void*)mPendingSave.frames);
        OSUnlockMutex(&mMutex);
        return false;
    }
    OSReport("[SC] cycleAndSave: extracting %u frames to pending\n", mCount);
    mPendingSave.frames = mSlots;
    mPendingSave.count  = mCount;
    mSlots = (CapturedFrame *) memalign(4, CAPACITY * sizeof(CapturedFrame));
    if (mSlots) {
        memset(mSlots, 0, CAPACITY * sizeof(CapturedFrame));
        OSReport("[SC] cycleAndSave: allocated fresh slots OK\n");
    } else {
        OSReport("[SC] cycleAndSave: FAILED to allocate fresh slots, KEEPING pending alive, mSlots=null\n");
        // Keep mPendingSave alive — the extracted frames are preserved for the save thread.
        // mSlots stays null; push() safely returns false until allocSlots() is called.
    }
    mHead  = 0;
    mCount = 0;
    OSUnlockMutex(&mMutex);
    OSReport("[SC] cycleAndSave: returning %d\n", mPendingSave.frames != nullptr);
    return mPendingSave.frames != nullptr;
}

void RingBuffer::emergencyCycle() {
    OSLockMutex(&mMutex);
    OSReport("[SC] emergencyCycle: freeing %u frames, pendingSave=%p\n",
             mCount, (void*)mPendingSave.frames);
    for (uint32_t i = 0; i < CAPACITY; i++) {
        if (mSlots && mSlots[i].data) {
            free(mSlots[i].data);
            mSlots[i].data = nullptr;
            mSlots[i].size = 0;
        }
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