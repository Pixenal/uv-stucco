#pragma once
#include "Types.h"

typedef struct {
	void *file;
} UvgpFile;

int32_t uvgpFileOpen(UvgpFile *file, char *filePath);
int32_t uvgpFileWrite(UvgpFile *file, unsigned char *data, int32_t dataLength);
int32_t uvgpFileClose(UvgpFile *file);
