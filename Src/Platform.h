#pragma once
#include "Types.h"

typedef struct {
	void *file;
} PlatformFile;

int32_t ruvmPlatformFileOpen(void **file, char *filePath, int32_t action, RuvmAllocator *pAlloc);
int32_t ruvmPlatformFileWrite(void *file, unsigned char *data, int32_t dataSize);
int32_t ruvmPlatformFileRead(void *file, unsigned char *data, int32_t bytesToRead);
int32_t ruvmPlatformFileClose(void *file);
