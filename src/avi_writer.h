#pragma once

#include "ring_buffer.h"
#include <stdint.h>
#include <stdio.h>
#include <string>

bool writeAVI(const std::string &path, RingBuffer &buf,
              const int16_t *audioData = nullptr,
              size_t audioSamples = 0,
              uint32_t audioSampleRate = 48000);

// Write AVI from a flat array of CapturedFrame (used when ring buffer slots can't be allocated)
bool writeAVI(const std::string &path,
              CapturedFrame *frames, uint32_t frameCount,
              const int16_t *audioData = nullptr,
              size_t audioSamples = 0,
              uint32_t audioSampleRate = 48000);
