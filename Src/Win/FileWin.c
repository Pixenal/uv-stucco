#include <windows.h>
#include <stdbool.h>
#include <stdio.h>

#include <PlatformIo.h>
#include <Types.h>

typedef struct {
	HANDLE *pHFile;
	StucAlloc alloc;
} PlatformContext;

I32 stucPlatformFileOpen(
	void **file,
	const char *filePath,
	I32 action,
	const StucAlloc *pAlloc
) {
	DWORD access;
	DWORD disposition;
	switch (action) {
		case 0:
			access = GENERIC_WRITE;
			disposition = CREATE_ALWAYS;
			break;
		case 1:
			access = GENERIC_READ;
			disposition = OPEN_EXISTING;
			break;
		default:
			printf("Failed to open file. Invalid action passed to function\n");
			return 1;
	}
	PlatformContext *pState = pAlloc->pMalloc(sizeof(PlatformContext));
	pState->alloc = *pAlloc;
	pState->pHFile = CreateFile(
		filePath,
		access,
		false,
		NULL,
		disposition,
		FILE_ATTRIBUTE_NORMAL, NULL
	);
	if (pState->pHFile == INVALID_HANDLE_VALUE) {
		DWORD lastError =  GetLastError();
		printf("Failed to open UVGP file for write. Error: %lu\n", lastError);
		return 1;
	}
	*file = pState;
	return 0;
}

I32 stucPlatformFileWrite(
	void *file,
	const unsigned char *data,
	I32 dataSize
) {
	PlatformContext *pState = file;
	DWORD bytesWritten;
	I32 returnCode = WriteFile(pState->pHFile, data, dataSize, &bytesWritten, NULL);
	if (!returnCode) {
		DWORD lastError =  GetLastError();
		printf("Failed to write to UVGP file. Error: %lu\n", lastError);
		return 1;
	}
	if (bytesWritten != dataSize) {
		printf("Failed. Number of bytes written to UVGP does not \
			match data length\n");
	}
	return 0;
}

I32 stucPlatformFileRead(
	void *file,
	unsigned char *data,
	I32 bytesToRead
) {
	PlatformContext *pState = file;
	DWORD bytesRead;
	I32 returnCode = ReadFile(pState->pHFile, data, bytesToRead, &bytesRead, NULL);
	if (!returnCode) {
		DWORD lastError =  GetLastError();
		printf("Failed to read from UVGP file. Error: %lu\n", lastError);
		return 1;
	}
	if (bytesRead != bytesToRead) {
		printf("Failed. Number of bytes read from UVGP file does \
			not match specififed amount\n");
	}
	return 0;
}

I32 stucPlatformFileClose(void *file) {
	PlatformContext *pState = file;
	CloseHandle(pState->pHFile);
	pState->alloc.pFree(pState);
	return 0;
}
