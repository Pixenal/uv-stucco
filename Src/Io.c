#define VERT_ATTRIBUTE_AMOUNT 3
#define LOOP_ATTRIBUTE_AMOUNT 3
#define ENCODE_DECODE_BUFFER_LENGTH 34

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

#include <zlib.h>

#include <Io.h>
#include <QuadTree.h>
#include <MapFile.h>
#include <Context.h>
#include <Platform.h>
#include <MathUtils.h>
#include <AttribUtils.h>

//TODO add info to header to identify file as an ruvm,
//to prevent accidentally trying to load a different format

typedef struct {
	unsigned char *pString;
	int32_t nextBitIndex;
	int32_t byteIndex;
} ByteString;

static int32_t decodeSingleBit(ByteString *byteString) {
	int32_t value = byteString->pString[byteString->byteIndex];
	value >>= byteString->nextBitIndex;
	value &= 1;
	byteString->nextBitIndex++;
	byteString->byteIndex += byteString->nextBitIndex >= 8;
	byteString->nextBitIndex %= 8;
	return value;
}

static void encodeValue(ByteString *byteString, uint8_t *value, int32_t lengthInBits) {
	uint8_t valueBuffer[ENCODE_DECODE_BUFFER_LENGTH] = {0};
	int32_t lengthInBytes = lengthInBits / 8;
	lengthInBytes += (lengthInBits - lengthInBytes * 8) > 0;
	for (int32_t i = 1; i <= lengthInBytes; ++i) {
		valueBuffer[i] = value[i - 1];
	}
	for (int32_t i = lengthInBytes - 1; i >= 1; --i) {
		valueBuffer[i] <<= byteString->nextBitIndex;
		uint8_t nextByteCopy = valueBuffer[i - 1];
		nextByteCopy >>= 8 - byteString->nextBitIndex;
		valueBuffer[i] |= nextByteCopy;
	}
	int32_t writeUpTo = lengthInBytes + (byteString->nextBitIndex > 0);
	for (int32_t i = 0; i < writeUpTo; ++i) {
		byteString->pString[byteString->byteIndex + i] |= valueBuffer[i + 1];
	}
	byteString->nextBitIndex = byteString->nextBitIndex + lengthInBits;
	byteString->byteIndex += byteString->nextBitIndex / 8;
	byteString->nextBitIndex %= 8;
}

static void encodeString(ByteString *byteString, uint8_t *string) {
	int32_t lengthInBits = (strlen((char *)string) + 1) * 8;
	int32_t lengthInBytes = lengthInBits / 8;
	byteString->byteIndex += byteString->nextBitIndex > 0;
	byteString->nextBitIndex = 0;
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		byteString->pString[byteString->byteIndex] = string[i];
		byteString->byteIndex++;
	}
}

static void decodeValue(ByteString *byteString, uint8_t *value, int32_t lengthInBits) {
	int32_t lengthInBytes = lengthInBits / 8;
	int32_t bitDifference = lengthInBits - lengthInBytes * 8;
	lengthInBytes += bitDifference > 0;
	uint8_t buffer[ENCODE_DECODE_BUFFER_LENGTH] = {0};
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		buffer[i] = byteString->pString[byteString->byteIndex + i];
	}
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		buffer[i] >>= byteString->nextBitIndex;
		uint8_t nextByteCopy = buffer[i + 1];
		nextByteCopy <<= 8 - byteString->nextBitIndex;
		buffer[i] |= nextByteCopy;
	}
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		value[i] = buffer[i];
	}
	uint8_t mask = UCHAR_MAX >> ((8 - bitDifference) % 8);
	value[lengthInBytes - 1] &= mask;
	byteString->nextBitIndex = byteString->nextBitIndex + lengthInBits;
	byteString->byteIndex += byteString->nextBitIndex / 8;
	byteString->nextBitIndex %= 8;
}

static void decodeString(ByteString *byteString, char *string, int32_t maxLen) {
	byteString->byteIndex += byteString->nextBitIndex > 0;
	uint8_t *dataPtr = byteString->pString + byteString->byteIndex;
	int32_t i = 0;
	for (; i < maxLen && dataPtr[i]; ++i) {
		string[i] = dataPtr[i];
	}
	string[i] = 0;
	byteString->byteIndex += i + 1;
	byteString->nextBitIndex = 0;
}

/*
static void ruvmWriteDebugImage(Cell *rootCell) {
	FILE* file;
	file = fopen("/run/media/calebdawson/Tuna/workshop_folders/RUVM/DebugOutput_LoadedFile.ppm", "w");
	fprintf(file, "P3\n%d %d\n255\n", 512, 512);
	for (int32_t i = 0; i < 512; ++i) {
		for (int32_t j = 0; j < 512; ++j) {
			unsigned char red, green, blue, alpha;
			float pixelSize = 1.0 / (float)512;
			V2_F32 pixelPos = {.x = pixelSize * j, .y = pixelSize * i};
			Cell *enclosingCell = findEnclosingCell(rootCell, pixelPos);
			red = (unsigned char)(enclosingCell->cellIndex % 256);
			green = (unsigned char)(enclosingCell->cellIndex << 3 % 256);
			blue = (unsigned char)(enclosingCell->cellIndex << 1 % 256);
			alpha = (unsigned char)1.0;
			fprintf(file, "%d %d %d\n", red, green, blue);
		}
	}
	fclose(file);
}
*/

static int32_t getTotalAttribSize(AttribArray *pAttribs) {
	int32_t totalSize = 0;
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		totalSize += getAttribSize(pAttribs->pArr[i].type) * 8;
	}
	return totalSize;
}

static int32_t getTotalAttribMetaSize(AttribArray *pAttribs) {
	int32_t totalSize = pAttribs->count * 16;
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		totalSize += (strlen(pAttribs->pArr[i].name) + 1) * 8;
	}
	return totalSize;
}

static void encodeAttribs(ByteString *pData, AttribArray *pAttribs,
                          int32_t dataLen) {
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		if (pAttribs->pArr[i].type == RUVM_ATTRIB_STRING) {
			for (int32_t j = 0; j < dataLen; ++j) {
				void *pString = attribAsVoid(pAttribs->pArr + i, j);
				encodeString(pData, pString);
			}
		}
		else {
			int32_t attribSize = getAttribSize(pAttribs->pArr[i].type) * 8;
			for (int32_t j = 0; j < dataLen; ++j) {
				encodeValue(pData, attribAsVoid(pAttribs->pArr + i, j), attribSize);
			}
		}
	}
}

static void encodeAttribMeta(ByteString *pData, AttribArray *pAttribs) {
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		encodeValue(pData, (uint8_t *)&pAttribs->pArr[i].type, 16);
		encodeString(pData, (uint8_t *)pAttribs->pArr[i].name);
	}
}

void ruvmWriteRuvmFile(RuvmContext pContext, RuvmMesh *pMesh) {
	ByteString header;
	ByteString data;

	//calculate total size of attribute header info
	int64_t meshAttribMetaSize = getTotalAttribMetaSize(&pMesh->meshAttribs);
	int64_t faceAttribMetaSize = getTotalAttribMetaSize(&pMesh->faceAttribs);
	int64_t loopAttribMetaSize = getTotalAttribMetaSize(&pMesh->loopAttribs);
	int64_t edgeAttribMetaSize = getTotalAttribMetaSize(&pMesh->edgeAttribs);
	int64_t vertAttribMetaSize = getTotalAttribMetaSize(&pMesh->vertAttribs);

	int64_t headerSizeInBits = 64 + //compressed data size
	                           64 + //uncompressed data size
							   meshAttribMetaSize +
	                           32 + //face attrib count
	                           faceAttribMetaSize +
							   32 + //loop attrib count
							   loopAttribMetaSize +
							   32 + //edge attrib count
							   edgeAttribMetaSize +
							   32 + //vert attrib count
							   vertAttribMetaSize +
	                           32 + //face count
	                           32 + //loop count
							   32 + //edge count
	                           32 + //vert count
							   128; //reserved for future extensions

	//calculate total size of attribute data
	int64_t meshAttribSize = getTotalAttribSize(&pMesh->meshAttribs);
	int64_t faceAttribSize = getTotalAttribSize(&pMesh->faceAttribs);
	int64_t loopAttribSize = getTotalAttribSize(&pMesh->loopAttribs);
	int64_t edgeAttribSize = getTotalAttribSize(&pMesh->edgeAttribs);
	int64_t vertAttribSize = getTotalAttribSize(&pMesh->vertAttribs);

	int64_t dataSizeInBits = meshAttribSize +
	                         32 * (int64_t)pMesh->faceCount + //faces
	                         faceAttribSize * pMesh->faceCount + //face attributes
							 32 * (int64_t)pMesh->loopCount + //loops
							 32 * (int64_t)pMesh->loopCount + //edges
	                         loopAttribSize * pMesh->loopCount + //loop attributes
							 edgeAttribSize * pMesh->edgeCount + //edge attributes
							 vertAttribSize * pMesh->vertCount; //vert attributes
	
	int32_t headerSizeInBytes = (int32_t)(headerSizeInBits / 8 + 2);
	int64_t dataSizeInBytes = dataSizeInBits / 8 + 2;
	data.byteIndex = 0;
	data.nextBitIndex = 0;
	header.byteIndex = 0;
	header.nextBitIndex = 0;

	//encode data
	data.pString = pContext->alloc.pCalloc(dataSizeInBytes, 1);
	encodeAttribs(&data, &pMesh->meshAttribs, 1);
	for (int32_t i = 0; i < pMesh->faceCount; ++i) {
		encodeValue(&data, (uint8_t *)&pMesh->pFaces[i], 32);
	}
	encodeAttribs(&data, &pMesh->faceAttribs, pMesh->faceCount);
	for (int32_t i = 0; i < pMesh->loopCount; ++i) {
		encodeValue(&data, (uint8_t *)&pMesh->pLoops[i], 32);
		encodeValue(&data, (uint8_t *)&pMesh->pEdges[i], 32);
	}
	encodeAttribs(&data, &pMesh->loopAttribs, pMesh->loopCount);
	encodeAttribs(&data, &pMesh->edgeAttribs, pMesh->edgeCount);
	encodeAttribs(&data, &pMesh->vertAttribs, pMesh->vertCount);

	//compress data
	//TODO convert to use proper zlib inflate and deflate calls
	//compress and decompress are not context independent iirc
	int64_t dataSize = data.byteIndex + (data.nextBitIndex > 0);
	int64_t dataSizeExtra = dataSize / 1000;
	dataSizeExtra += ((dataSize * 1000) - dataSize) > 0;
	dataSizeExtra += 12;
	unsigned long compressedDataSize = dataSize + dataSizeExtra;
	uint8_t *compressedData = pContext->alloc.pMalloc(compressedDataSize);
	int32_t zResult = compress(compressedData, &compressedDataSize, data.pString, dataSize);
	switch(zResult) {
		case Z_OK:
			printf("Successfully compressed RUVM data\n");
			break;
		case Z_MEM_ERROR:
			printf("Failed to compress RUVM data, memory error\n");
			break;
		case Z_BUF_ERROR:
			printf("Failed to compress RUVM data, output buffer too small\n");
			break;
	}

	printf("Compressed data is %lu long\n", compressedDataSize);

	//encode header
	header.pString = pContext->alloc.pCalloc(headerSizeInBytes, 1);
	int32_t version = 100;
	encodeValue(&header, (uint8_t *)&version, 16);
	encodeValue(&header, (uint8_t *)&compressedDataSize, 64);
	encodeValue(&header, (uint8_t *)&dataSize, 64);

	encodeValue(&header, (uint8_t *)&pMesh->meshAttribs.count, 32);
	encodeAttribMeta(&header, &pMesh->meshAttribs);
	encodeValue(&header, (uint8_t *)&pMesh->faceAttribs.count, 32);
	encodeAttribMeta(&header, &pMesh->faceAttribs);
	encodeValue(&header, (uint8_t *)&pMesh->loopAttribs.count, 32);
	encodeAttribMeta(&header, &pMesh->loopAttribs);
	encodeValue(&header, (uint8_t *)&pMesh->edgeAttribs.count, 32);
	encodeAttribMeta(&header, &pMesh->edgeAttribs);
	encodeValue(&header, (uint8_t *)&pMesh->vertAttribs.count, 32);
	encodeAttribMeta(&header, &pMesh->vertAttribs);

	encodeValue(&header, (uint8_t *)&pMesh->faceCount, 32);
	encodeValue(&header, (uint8_t *)&pMesh->loopCount, 32);
	encodeValue(&header, (uint8_t *)&pMesh->edgeCount, 32);
	encodeValue(&header, (uint8_t *)&pMesh->vertCount, 32);

	//TODO CRC for uncompressed data
	
	void *pFile;
	pContext->io.pOpen(&pFile, "/run/media/calebdawson/Tuna/workshop_folders/RUVM/TestOutputDir/HardSurface.ruvm",
	                   0, &pContext->alloc);
	//pContext->io.pOpen(&pFile, "T:/workshop_folders/RUVM/TestOutputDir/CrownfallWall.ruvm",
	//                   0, &pContext->alloc);
	headerSizeInBytes = header.byteIndex + (header.nextBitIndex > 0);
	pContext->io.pWrite(pFile, (uint8_t *)&headerSizeInBytes, 4);
	pContext->io.pWrite(pFile, header.pString, headerSizeInBytes);
	pContext->io.pWrite(pFile, compressedData, (int32_t)compressedDataSize);
	pContext->io.pClose(pFile);

	pContext->alloc.pFree(header.pString);
	pContext->alloc.pFree(data.pString);

	printf("Finished RUVM export\n");
}

static void decodeAttribMeta(ByteString *pData, AttribArray *pAttribs) {
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		decodeValue(pData, (uint8_t *)&pAttribs->pArr[i].type, 16);
		int32_t maxNameLen = sizeof(pAttribs->pArr[i].name);
		decodeString(pData, (char *)&pAttribs->pArr[i].name, maxNameLen);
	}
}

static void decodeAttribs(RuvmContext pContext, ByteString *pData,
                          AttribArray *pAttribs, int32_t dataLen) {
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		int32_t attribSize = getAttribSize(pAttribs->pArr[i].type) * 8;
		pAttribs->pArr[i].pData = dataLen ?
			pContext->alloc.pCalloc(dataLen, attribSize) : NULL;
		for (int32_t j = 0; j < dataLen; ++j) {
			void *pAttribData = attribAsVoid(pAttribs->pArr + i, j);
			if (pAttribs->pArr[i].type == RUVM_ATTRIB_STRING) {
				decodeString(pData, pAttribData, attribSize);
			}
			else {
				decodeValue(pData, pAttribData, attribSize);
			}
		}
	}
}

static RuvmHeader decodeRuvmHeader(RuvmContext pContext, RuvmMap pMapFile, ByteString *headerByteString) {
	RuvmHeader header = {0};
	RuvmMesh *pMesh = &pMapFile->mesh.mesh;
	//printf("0\n");
	decodeValue(headerByteString, (uint8_t *)&header.version, 16);
	decodeValue(headerByteString, (uint8_t *)&header.dataSizeCompressed, 64);
	decodeValue(headerByteString, (uint8_t *)&header.dataSize, 64);

	decodeValue(headerByteString, (uint8_t *)&pMesh->meshAttribs.count, 32);
	pMesh->meshAttribs.pArr = pMesh->meshAttribs.count ?
		pContext->alloc.pCalloc(pMesh->meshAttribs.count, sizeof(RuvmAttrib)) : NULL;
	decodeAttribMeta(headerByteString, &pMesh->meshAttribs);

	decodeValue(headerByteString, (uint8_t *)&pMesh->faceAttribs.count, 32);
	pMesh->faceAttribs.pArr = pMesh->faceAttribs.count ?
		pContext->alloc.pCalloc(pMesh->faceAttribs.count, sizeof(RuvmAttrib)) : NULL;
	decodeAttribMeta(headerByteString, &pMesh->faceAttribs);

	decodeValue(headerByteString, (uint8_t *)&pMesh->loopAttribs.count, 32);
	pMesh->loopAttribs.pArr = pMesh->loopAttribs.count ?
		pContext->alloc.pCalloc(pMesh->loopAttribs.count, sizeof(RuvmAttrib)) : NULL;
	decodeAttribMeta(headerByteString, &pMesh->loopAttribs);

	decodeValue(headerByteString, (uint8_t *)&pMesh->edgeAttribs.count, 32);
	pMesh->edgeAttribs.pArr = pMesh->edgeAttribs.count ?
		pContext->alloc.pCalloc(pMesh->edgeAttribs.count, sizeof(RuvmAttrib)) : NULL;
	decodeAttribMeta(headerByteString, &pMesh->edgeAttribs);

	decodeValue(headerByteString, (uint8_t *)&pMesh->vertAttribs.count, 32);
	pMesh->vertAttribs.pArr = pMesh->vertAttribs.count ?
		pContext->alloc.pCalloc(pMesh->vertAttribs.count, sizeof(RuvmAttrib)) : NULL;
	decodeAttribMeta(headerByteString, &pMesh->vertAttribs);

	decodeValue(headerByteString, (uint8_t *)&pMesh->faceCount, 32);
	decodeValue(headerByteString, (uint8_t *)&pMesh->loopCount, 32);
	decodeValue(headerByteString, (uint8_t *)&pMesh->edgeCount, 32);
	decodeValue(headerByteString, (uint8_t *)&pMesh->vertCount, 32);

	return header;
}

static void decodeRuvmData(RuvmContext pContext, RuvmMap pMapFile,
                           ByteString *dataByteString) {
	RuvmMesh *pMesh = &pMapFile->mesh.mesh;

	decodeAttribs(pContext, dataByteString, &pMesh->meshAttribs, 1);

	pMesh->pFaces = pContext->alloc.pCalloc(pMesh->faceCount + 1, sizeof(int32_t));
	for (int32_t i = 0; i < pMesh->faceCount; ++i) {
		decodeValue(dataByteString, (uint8_t *)&pMesh->pFaces[i], 32);
	}
	pMesh->pFaces[pMesh->faceCount] = pMesh->loopCount;
	decodeAttribs(pContext, dataByteString, &pMesh->faceAttribs, pMesh->faceCount);

	pMesh->pLoops = pContext->alloc.pCalloc(pMesh->loopCount, sizeof(int32_t));
	pMesh->pEdges = pContext->alloc.pCalloc(pMesh->loopCount, sizeof(int32_t));
	for (int32_t i = 0; i < pMesh->loopCount; ++i) {
		decodeValue(dataByteString, (uint8_t *)&pMesh->pLoops[i], 32);
		decodeValue(dataByteString, (uint8_t *)&pMesh->pEdges[i], 32);
	}
	decodeAttribs(pContext, dataByteString, &pMesh->loopAttribs, pMesh->loopCount);
	decodeAttribs(pContext, dataByteString, &pMesh->edgeAttribs, pMesh->edgeCount);

	decodeAttribs(pContext, dataByteString, &pMesh->vertAttribs, pMesh->vertCount);
}

void ruvmLoadRuvmFile(RuvmContext pContext, RuvmMap pMapFile, char *filePath) {
	ByteString headerByteString = {0};
	ByteString dataByteString = {0};
	void *pFile;
	printf("Loading RUVM file: %s\n", filePath);
	pContext->io.pOpen(&pFile, filePath, 1, &pContext->alloc);
	uint8_t headerSize[4];
	pContext->io.pRead(pFile, headerSize, 4);
	int32_t headerSizeInt = *((int32_t *)headerSize);
	printf("Ruvm File Header Size: %d\n", headerSizeInt);
	printf("Header is %d bytes\n", headerSizeInt);
	headerByteString.pString = pContext->alloc.pMalloc(headerSizeInt);
	printf("Reading header\n");
	pContext->io.pRead(pFile, headerByteString.pString, headerSizeInt);
	printf("Decoding header\n");
	RuvmHeader header = decodeRuvmHeader(pContext, pMapFile, &headerByteString);
	uint8_t *dataByteStringRaw = pContext->alloc.pMalloc(header.dataSize);
	unsigned long dataSizeUncompressed = header.dataSize;
	printf("Reading data\n");
	pContext->io.pRead(pFile, dataByteStringRaw, header.dataSizeCompressed);
	pContext->io.pClose(pFile);
	dataByteString.pString = pContext->alloc.pMalloc(header.dataSize);
	printf("Decompressing data\n");
	int32_t zResult = uncompress(dataByteString.pString, &dataSizeUncompressed,
			                     dataByteStringRaw, header.dataSizeCompressed);
	pContext->alloc.pFree(dataByteStringRaw);
	switch(zResult) {
		case Z_OK:
			printf("Successfully decompressed RUVM file data\n");
			break;
		case Z_MEM_ERROR:
			printf("Failed to decompress RUVM file data. Memory error\n");
			break;
		case Z_BUF_ERROR:
			printf("Failed to decompress RUVM file data. Buffer was too small\n");
			break;
	}
	if (dataSizeUncompressed != header.dataSize) {
		printf("Failed to load RUVM file. Decompressed data size doesn't match header description\n");
		return;
	}
	printf("Decoding data\n");
	decodeRuvmData(pContext, pMapFile, &dataByteString);
	pContext->alloc.pFree(headerByteString.pString);
	pContext->alloc.pFree(dataByteString.pString);
	setSpecialAttribs(&pMapFile->mesh, 0x2e); //101110 - set all except for preserve
}

void ruvmIoSetCustom(RuvmContext pContext, RuvmIo *pIo) {
	if (!pIo->pOpen || !pIo->pClose || !pIo->pWrite || !pIo->pRead) {
		printf("Failed to set custom IO. One or more functions were NULL");
		abort();
	}
	pContext->io = *pIo;
}

void ruvmIoSetDefault(RuvmContext pContext) {
	pContext->io.pOpen = ruvmPlatformFileOpen;
	pContext->io.pClose = ruvmPlatformFileClose;
	pContext->io.pWrite = ruvmPlatformFileWrite;
    pContext->io.pRead = ruvmPlatformFileRead;
}
