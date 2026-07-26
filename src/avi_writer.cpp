#include "avi_writer.h"
#include "common.h"
#include "retain_vars.hpp"
#include "utils/logger.h"
#include <coreinit/time.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>

static void writeU16LE(FILE *f, uint16_t v) {
    uint8_t b[2] = { (uint8_t)(v & 0xFF), (uint8_t)(v >> 8) };
    fwrite(b, 1, 2, f);
}

static void writeU32LE(FILE *f, uint32_t v) {
    uint8_t b[4] = {
        (uint8_t)(v        & 0xFF),
        (uint8_t)((v >>  8) & 0xFF),
        (uint8_t)((v >> 16) & 0xFF),
        (uint8_t)((v >> 24) & 0xFF)
    };
    fwrite(b, 1, 4, f);
}

static void writeI32LE(FILE *f, int32_t v)  { writeU32LE(f, (uint32_t)v); }
static void writeFourCC(FILE *f, const char *cc) { fwrite(cc, 1, 4, f); }

static long beginChunk(FILE *f, const char *fourCC) {
    writeFourCC(f, fourCC);
    long off = ftell(f);
    writeU32LE(f, 0);
    return off;
}

static long beginList(FILE *f, const char *type) {
    writeFourCC(f, "LIST");
    long off = ftell(f);
    writeU32LE(f, 0);
    writeFourCC(f, type);
    return off;
}

static void endChunk(FILE *f, long sizeOff) {
    long cur = ftell(f);
    fseek(f, sizeOff, SEEK_SET);
    writeU32LE(f, (uint32_t)(cur - sizeOff - 4));
    fseek(f, cur, SEEK_SET);
}

struct IdxEntry {
    char     ckid[4];
    uint32_t flags;
    uint32_t offset;
    uint32_t size;
};

static uint32_t  *sOutBuf   = nullptr;
static uint32_t   sOutCap   = 0;
static IdxEntry  *sIdxBuf   = nullptr;
static uint32_t   sIdxCap   = 0;

static void writeWaveFormatEx(FILE *fp, uint16_t channels, uint32_t sampleRate,
                               uint16_t bitsPerSample) {
    uint16_t blockAlign = channels * bitsPerSample / 8;
    writeU16LE(fp, 0x0001);
    writeU16LE(fp, channels);
    writeU32LE(fp, sampleRate);
    writeU32LE(fp, sampleRate * blockAlign);
    writeU16LE(fp, blockAlign);
    writeU16LE(fp, bitsPerSample);
    writeU16LE(fp, 0);
}

struct FrameAccess {
    CapturedFrame *frames;
    uint32_t       count;
};

static bool writeAVIImpl(const std::string &path, const FrameAccess &fa,
                         const int16_t *audioData, size_t audioSamples,
                         uint32_t audioSampleRate) {

    bool hasAudio = (audioData != nullptr && audioSamples > 0);

    if (fa.count == 0) {
        OSReport("[SC] writeAVI: no frames to write\n");
        return false;
    }

    constexpr uint32_t framePeriodUs = 1000000u / 30;

    uint32_t neededOut = fa.count * 2;
    if (neededOut > sOutCap) {
        uint32_t *newBuf = (uint32_t *) realloc(sOutBuf, neededOut * sizeof(uint32_t));
        if (!newBuf) { OSReport("[SC] writeAVI: OOM outFrames\n"); return false; }
        sOutBuf = newBuf;
        sOutCap = neededOut;
    }

    uint32_t outCount = 0;
    for (uint32_t i = 0; i < fa.count; i++) {
        if (outCount >= sOutCap) {
            uint32_t newCap = sOutCap ? sOutCap * 2 : 256;
            uint32_t *newBuf = (uint32_t *) realloc(sOutBuf, newCap * sizeof(uint32_t));
            if (!newBuf) { OSReport("[SC] writeAVI: OOM outFrames (grow)\n"); return false; }
            sOutBuf = newBuf;
            sOutCap = newCap;
        }
        sOutBuf[outCount++] = i;

        if (i + 1 < fa.count) {
            uint64_t dt   = fa.frames[i + 1].timestamp - fa.frames[i].timestamp;
            uint32_t dtUs = (uint32_t) OSTicksToMicroseconds(dt);
            if (dtUs > framePeriodUs) {
                uint32_t extraFrames = (dtUs + framePeriodUs / 2) / framePeriodUs - 1;
                if (extraFrames > 30) extraFrames = 30;
                for (uint32_t d = 0; d < extraFrames; d++) {
                    if (outCount >= sOutCap) {
                        uint32_t newCap = sOutCap ? sOutCap * 2 : 256;
                        uint32_t *newBuf = (uint32_t *) realloc(sOutBuf, newCap * sizeof(uint32_t));
                        if (!newBuf) { OSReport("[SC] writeAVI: OOM outFrames (dup)\n"); return false; }
                        sOutBuf = newBuf;
                        sOutCap = newCap;
                    }
                    sOutBuf[outCount++] = i;
                }
            }
        }
    }

    uint32_t totalFrames = outCount;

    uint32_t maxFrameSize = 0;
    for (uint32_t i = 0; i < fa.count; i++)
        if (fa.frames[i].size > maxFrameSize)
            maxFrameSize = (uint32_t) fa.frames[i].size;

    uint32_t videoWidth  = fa.frames[0].width;
    uint32_t videoHeight = fa.frames[0].height;

    uint32_t audioByteSize = hasAudio ? (uint32_t)(audioSamples * sizeof(int16_t)) : 0;
    uint32_t audioBlockAlign = 4;
    uint32_t audioStreamLen  = hasAudio ? (uint32_t)(audioSamples / 2) : 0;

    OSReport("[SC] writeAVI: %u src frames -> %u output%s\n",
             fa.count, totalFrames, hasAudio ? " + PCM audio" : "");

    FILE *fp = fopen(path.c_str(), "wb");
    if (!fp) { OSReport("[SC] writeAVI: failed to open %s\n", path.c_str()); return false; }
    setvbuf(fp, nullptr, _IOFBF, 256 * 1024);

    writeFourCC(fp, "RIFF");
    long riffSz = ftell(fp);
    writeU32LE(fp, 0);
    writeFourCC(fp, "AVI ");

    long hdrlSz = beginList(fp, "hdrl");
    {
        long sz = beginChunk(fp, "avih");
        writeU32LE(fp, framePeriodUs);
        writeU32LE(fp, maxFrameSize * 30u + audioByteSize / (totalFrames > 1 ? totalFrames : 1));
        writeU32LE(fp, 0); writeU32LE(fp, 0x10);
        writeU32LE(fp, totalFrames); writeU32LE(fp, 0);
        writeU32LE(fp, hasAudio ? 2u : 1u); writeU32LE(fp, maxFrameSize);
        writeU32LE(fp, videoWidth); writeU32LE(fp, videoHeight);
        writeU32LE(fp, 0); writeU32LE(fp, 0);
        writeU32LE(fp, 0); writeU32LE(fp, 0);
        writeU32LE(fp, 0); writeU32LE(fp, 0);
        endChunk(fp, sz);
    }
    {
        long strlSz = beginList(fp, "strl");
        {
            long sz = beginChunk(fp, "strh");
            writeFourCC(fp, "vids"); writeFourCC(fp, "MJPG");
            writeU32LE(fp, 0); writeU16LE(fp, 0); writeU16LE(fp, 0);
            writeU32LE(fp, 0); writeU32LE(fp, 1); writeU32LE(fp, 30u);
            writeU32LE(fp, 0); writeU32LE(fp, totalFrames);
            writeU32LE(fp, maxFrameSize); writeU32LE(fp, (uint32_t)-1);
            writeU32LE(fp, 0); writeU16LE(fp, 0); writeU16LE(fp, 0);
            writeU16LE(fp, (uint16_t)videoWidth); writeU16LE(fp, (uint16_t)videoHeight);
            endChunk(fp, sz);
        }
        {
            long sz = beginChunk(fp, "strf");
            writeU32LE(fp, 40); writeI32LE(fp, (int32_t)videoWidth);
            writeI32LE(fp, (int32_t)videoHeight); writeU16LE(fp, 1); writeU16LE(fp, 24);
            fwrite("MJPG", 1, 4, fp); writeU32LE(fp, maxFrameSize);
            writeI32LE(fp, 0); writeI32LE(fp, 0); writeU32LE(fp, 0); writeU32LE(fp, 0);
            endChunk(fp, sz);
        }
        endChunk(fp, strlSz);
    }
    if (hasAudio) {
        long strlSz = beginList(fp, "strl");
        {
            long sz = beginChunk(fp, "strh");
            writeFourCC(fp, "auds"); writeU32LE(fp, 0); writeU32LE(fp, 0);
            writeU16LE(fp, 0); writeU16LE(fp, 0); writeU32LE(fp, 0); writeU32LE(fp, 1);
            writeU32LE(fp, audioSampleRate); writeU32LE(fp, 0);
            writeU32LE(fp, audioStreamLen); writeU32LE(fp, audioByteSize);
            writeU32LE(fp, 0); writeU32LE(fp, audioBlockAlign);
            writeU16LE(fp, 0); writeU16LE(fp, 0); writeU16LE(fp, 0); writeU16LE(fp, 0);
            endChunk(fp, sz);
        }
        {
            long sz = beginChunk(fp, "strf");
            writeWaveFormatEx(fp, 2, audioSampleRate, 16);
            endChunk(fp, sz);
        }
        endChunk(fp, strlSz);
    }
    endChunk(fp, hdrlSz);

    long moviSz        = beginList(fp, "movi");
    long moviDataStart = moviSz + 4 + 4;

    uint32_t numIdxEntries = totalFrames + (hasAudio ? 1 : 0);
    if (numIdxEntries > sIdxCap) {
        IdxEntry *newBuf = (IdxEntry *) realloc(sIdxBuf, numIdxEntries * sizeof(IdxEntry));
        if (!newBuf) { fclose(fp); OSReport("[SC] writeAVI: OOM index\n"); return false; }
        sIdxBuf = newBuf;
        sIdxCap = numIdxEntries;
    }

    for (uint32_t i = 0; i < totalFrames; i++) {
        const CapturedFrame *frame = &fa.frames[sOutBuf[i]];
        uint32_t chunkOffset = (uint32_t)(ftell(fp) - moviDataStart);
        writeFourCC(fp, "00dc");
        writeU32LE(fp, (uint32_t)frame->size);
        fwrite(frame->data, 1, frame->size, fp);
        if (frame->size & 1) { uint8_t pad = 0; fwrite(&pad, 1, 1, fp); }
        memcpy(sIdxBuf[i].ckid, "00dc", 4);
        sIdxBuf[i].flags  = 0x10;
        sIdxBuf[i].offset = chunkOffset;
        sIdxBuf[i].size   = (uint32_t)frame->size;
    }

    if (hasAudio) {
        uint32_t chunkOffset = (uint32_t)(ftell(fp) - moviDataStart);
        writeFourCC(fp, "01wb");
        writeU32LE(fp, audioByteSize);
        uint8_t  leBuf[4096];
        uint32_t remaining = audioByteSize;
        const uint16_t *src16 = (const uint16_t *) audioData;
        while (remaining > 0) {
            uint32_t chunk = (remaining < sizeof(leBuf)) ? remaining : (uint32_t) sizeof(leBuf);
            uint32_t cnt = chunk / 2;
            for (uint32_t i = 0; i < cnt; i++) {
                uint16_t v = src16[i];
                leBuf[i*2 + 0] = (uint8_t)(v & 0xFF);
                leBuf[i*2 + 1] = (uint8_t)(v >> 8);
            }
            fwrite(leBuf, 1, chunk, fp);
            src16    += cnt;
            remaining -= chunk;
        }
        if (audioByteSize & 1) { uint8_t pad = 0; fwrite(&pad, 1, 1, fp); }
        memcpy(sIdxBuf[totalFrames].ckid, "01wb", 4);
        sIdxBuf[totalFrames].flags  = 0x10;
        sIdxBuf[totalFrames].offset = chunkOffset;
        sIdxBuf[totalFrames].size   = audioByteSize;
    }

    endChunk(fp, moviSz);
    {
        long sz = beginChunk(fp, "idx1");
        for (uint32_t i = 0; i < numIdxEntries; i++) {
            fwrite(sIdxBuf[i].ckid, 1, 4, fp);
            writeU32LE(fp, sIdxBuf[i].flags);
            writeU32LE(fp, sIdxBuf[i].offset);
            writeU32LE(fp, sIdxBuf[i].size);
        }
        endChunk(fp, sz);
    }
    endChunk(fp, riffSz);
    fclose(fp);

    OSReport("[SC] writeAVI: done -> %s\n", path.c_str());
    return true;
}

bool writeAVI(const std::string &path, RingBuffer &buf,
              const int16_t *audioData, size_t audioSamples,
              uint32_t audioSampleRate) {
    buf.lock();
    uint32_t cnt = buf.count();
    if (cnt == 0) { buf.unlock(); return false; }
    CapturedFrame *tmp = (CapturedFrame *) malloc(cnt * sizeof(CapturedFrame));
    if (!tmp) { buf.unlock(); OSReport("[SC] writeAVI: OOM tmp\n"); return false; }
    for (uint32_t i = 0; i < cnt; i++)
        tmp[i] = *buf.get(i);
    buf.unlock();

    FrameAccess fa;
    fa.frames = tmp;
    fa.count  = cnt;
    bool ok = writeAVIImpl(path, fa, audioData, audioSamples, audioSampleRate);
    free(tmp);
    return ok;
}

bool writeAVI(const std::string &path,
              CapturedFrame *frames, uint32_t frameCount,
              const int16_t *audioData, size_t audioSamples,
              uint32_t audioSampleRate) {
    FrameAccess fa;
    fa.frames = frames;
    fa.count  = frameCount;
    return writeAVIImpl(path, fa, audioData, audioSamples, audioSampleRate);
}
