#pragma once

#include "ring_buffer.h"
#include <stdint.h>
#include <stdio.h>
#include <string>

bool writeAVI(const std::string &path, RingBuffer &buf,
              const int16_t *audioData = nullptr,
              size_t audioSamples = 0,
              uint32_t audioSampleRate = 48000);
