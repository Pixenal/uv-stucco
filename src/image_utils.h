/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once
#include <uv_stucco_intern.h>
#include <uv_stucco.h>

void setPixelColor(const StucImage *pImage, I32 idx, const Color *pColor);
int32_t getPixelSize(StucImageType type);
void *offsetImagePtr(const StucImage *pImage, I32 offset);
