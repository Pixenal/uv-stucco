/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <stdio.h>

#include "platform_io.h"
#include <error.h>

typedef struct {
	FILE *pFile;
	const StucAlloc *pAlloc;
} PlatformContext;

StucResult stucPlatformFileOpen(
	void **file,
	const char *filePath,
	StucFileOpenType action,
	const StucAlloc *pAlloc
) {
	StucResult err = STUC_SUCCESS;
	STUC_ASSERT("", file && filePath && pAlloc);
	STUC_ASSERT("", action == 0 || action == 1);
	char *mode = "  ";
	switch (action) {
		case STUC_FILE_OPEN_WRITE:
			mode = "wb";
			break;
		case STUC_FILE_OPEN_READ:
			mode = "rb";
			break;
		default:
			STUC_RETURN_ERR(err, "Invalid action passed to function\n");
	}
	PlatformContext *pState = pAlloc->fpMalloc(sizeof(PlatformContext));
	*file = pState;
	pState->pAlloc = pAlloc;
	pState->pFile = fopen(filePath, mode);
	STUC_RETURN_ERR_IFNOT_COND(err, pState->pFile, "");
	return err;
}

StucResult stucPlatformFileWrite(
	void *file,
	const unsigned char *data,
	I32 dataSize
) {
	StucResult err = STUC_SUCCESS;
	PlatformContext *pState = file;
	STUC_ASSERT("", pState && pState->pFile && data && dataSize > 0);
	int32_t bytesWritten = fwrite(data, 1, dataSize, pState->pFile);
	STUC_RETURN_ERR_IFNOT_COND(
		err,
		bytesWritten == dataSize,
		"Number of bytes read does not match data len"
	);
	return err;
}

StucResult stucPlatformFileRead(
	void *file,
	unsigned char *data,
	I32 bytesToRead
) {
	StucResult err = STUC_SUCCESS;
	PlatformContext *pState = file;
	STUC_ASSERT("", pState && pState->pFile && data && bytesToRead > 0);
	int32_t bytesRead = fread(data, 1, bytesToRead, pState->pFile);
	STUC_RETURN_ERR_IFNOT_COND(
		err,
		bytesRead == bytesToRead,
		"Number of bytes read does not match data len"
	);
	return err;
}

StucResult stucPlatformFileClose(void *file) {
	StucResult err = STUC_SUCCESS;
	PlatformContext *pState = file;
	STUC_ASSERT("", pState && pState->pFile);
	fclose(pState->pFile);
	pState->pAlloc->fpFree(pState);
	return err;
}
