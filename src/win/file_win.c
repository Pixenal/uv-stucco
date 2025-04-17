/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>

#include <platform_io.h>
#include <types.h>
#include <error.h>

#define ERR_MESSAGE_MAX_LEN 128

typedef struct PlatformContext {
	HANDLE *pHFile;
	StucAlloc alloc;
} PlatformContext;

StucResult stucPlatformFileOpen(
	void **ppFile,
	const char *filePath,
	StucFileOpenType action,
	const StucAlloc *pAlloc
) {
	StucResult err = STUC_SUCCESS;
	DWORD access;
	DWORD disposition;
	switch (action) {
		case STUC_FILE_OPEN_WRITE:
			access = GENERIC_WRITE;
			disposition = CREATE_ALWAYS;
			break;
		case STUC_FILE_OPEN_READ:
			access = GENERIC_READ;
			disposition = OPEN_EXISTING;
			break;
		default:
			STUC_RETURN_ERR(err, "Invalid action passed to function\n");
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
		STUC_RETURN_ERR(err, message);
	}
	*ppFile = pState;
	return err;
}

StucResult stucPlatformFileGetSize(void *pFile, I64 *pSize) {
	StucResult err = STUC_SUCCESS;
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
		STUC_RETURN_ERR(err, message);
	}
	*pSize = size.QuadPart;
	return err;
}

StucResult stucPlatformFileWrite(
	void *pFile,
	const unsigned char *data,
	I32 dataSize
) {
	StucResult err = STUC_SUCCESS;
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
		STUC_RETURN_ERR(err, message);
	}
	STUC_RETURN_ERR_IFNOT_COND(
		err,
		(I32)bytesWritten == dataSize,
		"Number of bytes written does not match data len"
	);
	return err;
}

StucResult stucPlatformFileRead(
	void *pFile,
	unsigned char *data,
	I32 bytesToRead
) {
	StucResult err = STUC_SUCCESS;
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
		STUC_RETURN_ERR(err, message);
	}
	STUC_RETURN_ERR_IFNOT_COND(
		err,
		(I32)bytesRead == bytesToRead,
		"Number of bytes read does not match specififed amount\n"
	);
	return err;
}

StucResult stucPlatformFileClose(void *pFile) {
	StucResult err = STUC_SUCCESS;
	PlatformContext *pState = pFile;
	bool success = CloseHandle(pState->pHFile);
	pState->alloc.fpFree(pState);
	if (!success) {
		char message[ERR_MESSAGE_MAX_LEN] = {0};
		snprintf(message, ERR_MESSAGE_MAX_LEN, "Win error: %d\n", GetLastError());
		STUC_RETURN_ERR(err, message);
	}
	return err;
}
