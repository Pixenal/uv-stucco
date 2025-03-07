#pragma once
#include <UvStuccoIntern.h>
#include <UvStucco.h>

void setPixelColor(const StucImage *pImage, I32 idx, const Color *pColor);
int32_t getPixelSize(StucImageType type);
void *offsetImagePtr(const StucImage *pImage, I32 offset);
