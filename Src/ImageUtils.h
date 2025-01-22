#pragma once
#include <UvStuccoIntern.h>
#include <UvStucco.h>

void setPixelColor(StucImage *pImage, int32_t index, Color *pColor);
int32_t getPixelSize(StucImageType type);
void *offsetImagePtr(StucImage *pImage, int32_t offset);
