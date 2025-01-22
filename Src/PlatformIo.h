#pragma once
#include <stdint.h>
#include <Alloc.h>

typedef struct {
	void *file;
} PlatformFile;

int32_t uvsPlatformFileOpen(void **file, char *filePath, int32_t action, RuvmAlloc *pAlloc);
int32_t uvsPlatformFileWrite(void *file, unsigned char *data, int32_t dataSize);
int32_t uvsPlatformFileRead(void *file, unsigned char *data, int32_t bytesToRead);
int32_t uvsPlatformFileClose(void *file);
