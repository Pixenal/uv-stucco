#include "Platform.h"
#include <windows.h>
#include <stdbool.h>
#include <stdio.h>

int32_t uvgpFileOpen(UvgpFile *file, char *filePath) {
	HANDLE hFile = CreateFile(filePath, GENERIC_WRITE, false, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		DWORD lastError =  GetLastError();
		printf("Failed to open UVGP file for write. Error: %lu\n", lastError);
		return 1;
	}
	file->file = (void *)hFile;
	return 0;
}

int32_t uvgpFileWrite(UvgpFile *file, unsigned char *data, int32_t dataLength) {
	HANDLE hFile = (HANDLE)file->file;
	DWORD bytesWritten;
	int32_t returnCode = WriteFile(hFile, data, dataLength, &bytesWritten, NULL);
	if (!returnCode) {
		DWORD lastError =  GetLastError();
		printf("Failed to write to UVGP file. Error: %lu\n", lastError);
		return 1;
	}
	if (bytesWritten != dataLength) {
		printf("Failed. Number of bytes written to UVGP does not match data length\n");
	}
	return 0;
}

int32_t uvgpFileClose(UvgpFile *file) {
	HANDLE hFile = (HANDLE)file->file;
	CloseHandle(hFile);
	return 0;
}
