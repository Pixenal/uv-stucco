#pragma once
#include <stdint.h>

#include <Alloc.h>
#include <Types.h>

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
