#pragma once
#include <RuvmInternal.h>
#include <RUVM.h>

void setPixelColor(RuvmImage *pImage, int32_t index, Color *pColor);
int32_t getPixelSize(RuvmImageType type);
void *offsetImagePtr(RuvmImage *pImage, int32_t offset);
