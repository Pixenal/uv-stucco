#pragma once
#include <stdint.h>

#include <Alloc.h>
#include <Types.h>

typedef struct {
	void *file;
} PlatformFile;

I32 stucPlatformFileOpen(
	void **file,
	const char *filePath,
	I32 action,
	const StucAlloc *pAlloc
);
I32 stucPlatformFileWrite(void *file, const unsigned char *data, I32 dataSize);
I32 stucPlatformFileRead(void *file, unsigned char *data, I32 bytesToRead);
I32 stucPlatformFileClose(void *file);
