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

typedef enum FileOpenType {
	STUC_FILE_OPEN_WRITE,
	STUC_FILE_OPEN_READ
} FileOpenType;

StucResult stucPlatformFileOpen(
	void **ppFile,
	const char *filePath,
	FileOpenType action,
	const StucAlloc *pAlloc
);
StucResult stucPlatformFileGetSize(void *pFile, I64 *pSize);
StucResult stucPlatformFileWrite(void *pFile, const unsigned char *data, I32 dataSize);
StucResult stucPlatformFileRead(void *pFile, unsigned char *data, I32 bytesToRead);
StucResult stucPlatformFileClose(void *pFile);
