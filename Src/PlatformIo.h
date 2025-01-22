#pragma once
#include <stdint.h>
#include <Alloc.h>

typedef struct {
	void *file;
} PlatformFile;

int32_t stucPlatformFileOpen(void **file, char *filePath, int32_t action, StucAlloc *pAlloc);
int32_t stucPlatformFileWrite(void *file, unsigned char *data, int32_t dataSize);
int32_t stucPlatformFileRead(void *file, unsigned char *data, int32_t bytesToRead);
int32_t stucPlatformFileClose(void *file);
