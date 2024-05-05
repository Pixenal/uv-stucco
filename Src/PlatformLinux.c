#include "Platform.h"
#include <stdio.h>

typedef struct {
	FILE *pFile;
	RuvmAlloc alloc;
} PlatformContext;

int32_t ruvmPlatformFileOpen(void **file, char *filePath, int32_t action, RuvmAlloc *pAlloc) {
	char *mode = "   ";
	switch (action) {
		case 0:
			mode = "wb";
			break;
		case 1:
			mode = "rb";
			break;
		default:
			printf("Failed to open file. Invalid action passed to function\n");
			return 1;
	}
	PlatformContext *pState = pAlloc->pMalloc(sizeof(PlatformContext));
	*file = pState;
	pState->alloc = *pAlloc;
	pState->pFile = fopen(filePath, mode);
	if (!pState->pFile) {
		printf("Failed to open file. fopen returned NULL");
		return 1;
	}
	return 0;
}

int32_t ruvmPlatformFileWrite(void *file, unsigned char *data, int32_t dataSize) {
	PlatformContext *pState = file;
	int32_t bytesWritten = fwrite(data, 1, dataSize, pState->pFile);
	if (bytesWritten != dataSize) {
		printf("Failed to write to file. Bytes written does not equal specified amount\n");
		return 1;
	}
	return 0;
}

int32_t ruvmPlatformFileRead(void *file, unsigned char *data, int32_t bytesToRead) {
	PlatformContext *pState = file;
	int32_t bytesRead = fread(data, 1, bytesToRead, pState->pFile);
	if (bytesRead != bytesToRead) {
		printf("Failed to read file. Bytes read does not equal specified amount\n");
		return 1;
	}
	return 0;
}

int32_t ruvmPlatformFileClose(void *file) {
	PlatformContext *pState = file;
	fclose(pState->pFile);
	pState->alloc.pFree(pState);
	return 0;
}
