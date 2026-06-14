#pragma once

#include <stdint.h>
#include <stddef.h>

enum AudioSource {
    AUDIO_SRC_TV,
    AUDIO_SRC_DRC,
};

void captureAudioBuffer(AudioSource src, const void *buffer, uint32_t size);
void initAudioCapture();
void stopAudioCapture();
void getAudioData(AudioSource src, const int16_t **data, size_t *numSamples, uint32_t *sampleRate);
uint64_t getAudioWritePos(AudioSource src);
void clearAudioBuffer();
void destroyAudioCapture();
