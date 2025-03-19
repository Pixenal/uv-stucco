/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once
#include <stdint.h>

#include <alloc.h>
#include <types.h>

typedef struct {
	void *file;
} PlatformFile;

StucResult stucPlatformFileOpen(
	void **file,
	const char *filePath,
	I32 action,
	const StucAlloc *pAlloc
);
StucResult stucPlatformFileWrite(void *file, const unsigned char *data, I32 dataSize);
StucResult stucPlatformFileRead(void *file, unsigned char *data, I32 bytesToRead);
StucResult stucPlatformFileClose(void *file);
