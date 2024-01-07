#include "Platform.h"
#include <windows.h>
#include <stdbool.h>
#include <stdio.h>

int32_t uvgpFileOpen(UvgpFile *file, char *filePath, int32_t action) {
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
	LPSTR filePathAlt = "File.uvgp";
	HANDLE hFile = CreateFile(filePath, access, false, NULL, disposition, FILE_ATTRIBUTE_NORMAL, NULL);
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

int32_t uvgpFileRead(UvgpFile *file, unsigned char *data, int32_t bytesToRead) {
	HANDLE hFile = (HANDLE)file->file;
	DWORD bytesRead;
	int32_t returnCode = ReadFile(hFile, data, bytesToRead, &bytesRead, NULL);
	if (!returnCode) {
		DWORD lastError =  GetLastError();
		printf("Failed to read from UVGP file. Error: %lu\n", lastError);
		return 1;
	}
	if (bytesRead != bytesToRead) {
		printf("Failed. Number of bytes read from UVGP file does not match specififed amount\n");
	}
	return 0;
}

int32_t uvgpFileClose(UvgpFile *file) {
	HANDLE hFile = (HANDLE)file->file;
	CloseHandle(hFile);
	return 0;
}
