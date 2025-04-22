/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>

#include <platform_io.h>

#define ERR_MESSAGE_MAX_LEN 128

typedef struct PlatformContext {
	HANDLE *pHFile;
	PixalcFPtrs alloc;
} PlatformContext;

PixErr pixioFileOpen(
	void **ppFile,
	const char *filePath,
	PixioFileOpenType action,
	const PixalcFPtrs *pAlloc
) {
	PixErr err = PIX_ERR_SUCCESS;
	DWORD access;
	DWORD disposition;
	switch (action) {
		case PIX_IO_FILE_OPEN_WRITE:
			access = GENERIC_WRITE;
			disposition = CREATE_ALWAYS;
			break;
		case PIX_IO_FILE_OPEN_READ:
			access = GENERIC_READ;
			disposition = OPEN_EXISTING;
			break;
		default:
			PIX_ERR_RETURN(err, "Invalid action passed to function\n");
	}
	PlatformContext *pState = pAlloc->fpMalloc(sizeof(PlatformContext));
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
		char message[ERR_MESSAGE_MAX_LEN] = {0};
		snprintf(message, ERR_MESSAGE_MAX_LEN, "Win Error: %d\n", GetLastError());
		PIX_ERR_RETURN(err, message);
	}
	*ppFile = pState;
	return err;
}

PixErr pixioFileGetSize(void *pFile, int64_t *pSize) {
	PixErr err = PIX_ERR_SUCCESS;
	PlatformContext *pState = pFile;
	LARGE_INTEGER size = {0};
	if (!GetFileSizeEx(pState->pHFile, &size)) {
		char message[ERR_MESSAGE_MAX_LEN] = {0};
		snprintf(
			message,
			ERR_MESSAGE_MAX_LEN,
			"Win error: %d\n",
			GetLastError()
		);
		PIX_ERR_RETURN(err, message);
	}
	*pSize = size.QuadPart;
	return err;
}

PixErr pixioFileWrite(
	void *pFile,
	const unsigned char *data,
	int32_t dataSize
) {
	PixErr err = PIX_ERR_SUCCESS;
	PlatformContext *pState = pFile;
	DWORD bytesWritten;
	if (!WriteFile(pState->pHFile, data, dataSize, &bytesWritten, NULL)) {
		char message[ERR_MESSAGE_MAX_LEN] = {0};
		snprintf(
			message,
			ERR_MESSAGE_MAX_LEN,
			"Win error: %d\n",
			GetLastError()
		);
		PIX_ERR_RETURN(err, message);
	}
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		(int32_t)bytesWritten == dataSize,
		"Number of bytes written does not match data len"
	);
	return err;
}

PixErr pixioFileRead(
	void *pFile,
	unsigned char *data,
	int32_t bytesToRead
) {
	PixErr err = PIX_ERR_SUCCESS;
	PlatformContext *pState = pFile;
	DWORD bytesRead;
	if (!ReadFile(pState->pHFile, data, bytesToRead, &bytesRead, NULL)) {
		char message[ERR_MESSAGE_MAX_LEN] = {0};
		snprintf(
			message,
			ERR_MESSAGE_MAX_LEN,
			"Win error: %d\n",
			GetLastError()
		);
		PIX_ERR_RETURN(err, message);
	}
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		(int32_t)bytesRead == bytesToRead,
		"Number of bytes read does not match specififed amount\n"
	);
	return err;
}

PixErr pixioFileClose(void *pFile) {
	PixErr err = PIX_ERR_SUCCESS;
	PlatformContext *pState = pFile;
	bool success = CloseHandle(pState->pHFile);
	pState->alloc.fpFree(pState);
	if (!success) {
		char message[ERR_MESSAGE_MAX_LEN] = {0};
		snprintf(message, ERR_MESSAGE_MAX_LEN, "Win error: %d\n", GetLastError());
		PIX_ERR_RETURN(err, message);
	}
	return err;
}
