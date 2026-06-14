#pragma once

#include <gx2/surface.h>

void captureFrame(GX2ColorBuffer *srcBuffer, GX2SurfaceFormat srcFormat, bool isTV);

void resetCaptureTiming();

void startEncodeThread();
void stopEncodeThread(bool flushSync = true);

void waitForEncodeDrain();
