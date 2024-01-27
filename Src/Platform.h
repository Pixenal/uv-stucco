#pragma once
#include "Types.h"

typedef struct {
	void *file;
} PlatformFile;

int32_t platformFileOpen(PlatformFile *file, char *filePath, int32_t action);
int32_t platformFileWrite(PlatformFile *file, unsigned char *data, int32_t dataSize);
int32_t platformFileRead(PlatformFile *file, unsigned char *data, int32_t bytesToRead);
int32_t platformFileClose(PlatformFile *file);
