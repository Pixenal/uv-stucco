#define VERT_ATTRIBUTE_AMOUNT 3
#define LOOP_ATTRIBUTE_AMOUNT 3
#define ENCODE_DECODE_BUFFER_LENGTH 5

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

#include <zlib.h>

#include <Types.h>
#include <Io.h>
#include <QuadTree.h>
#include <MapFile.h>
#include <Context.h>
#include <Platform.h>

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
	uint8_t valueBuffer[10] = {0};
	int32_t lengthInBytes = lengthInBits / 8;
	lengthInBytes += (lengthInBits - lengthInBytes * 8) > 0;
	for (int32_t i = 1; i <= lengthInBytes; ++i) {
		valueBuffer[i] = value[i - 1];
	}
	for (int32_t i = 7; i >= 1; --i) {
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

static void encodeString(ByteString *byteString, uint8_t *string, int32_t lengthInBits) {
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
	uint8_t buffer[ENCODE_DECODE_BUFFER_LENGTH + 1] = {0};
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
		value[i] |= buffer[i];
	}
	uint8_t mask = UCHAR_MAX >> ((8 - bitDifference) % 8);
	value[lengthInBytes - 1] &= mask;
	byteString->nextBitIndex = byteString->nextBitIndex + lengthInBits;
	byteString->byteIndex += byteString->nextBitIndex / 8;
	byteString->nextBitIndex %= 8;
}

static void decodeString(ByteString *byteString, char *string, int32_t stringSize) {
	byteString->byteIndex += byteString->nextBitIndex > 0;
	byteString->nextBitIndex = 0;
	uint8_t *dataPtr = byteString->pString + byteString->byteIndex;
	char *lastChar = string + (stringSize - 1);
	*lastChar = 1;
	do {
		*string = *dataPtr;
	} while(*++dataPtr && !*++string);
	*lastChar = 0;
	byteString->byteIndex += stringSize - (lastChar - string) + 1;
}

void ruvmWriteDebugImage(Cell *rootCell) {
	/*
	FILE* file;
	file = fopen("/run/media/calebdawson/Tuna/workshop_folders/RUVM/DebugOutput_LoadedFile.ppm", "w");
	fprintf(file, "P3\n%d %d\n255\n", 512, 512);
	for (int32_t i = 0; i < 512; ++i) {
		for (int32_t j = 0; j < 512; ++j) {
			unsigned char red, green, blue, alpha;
			float pixelSize = 1.0 / (float)512;
			Vec2 pixelPos = {.x = pixelSize * j, .y = pixelSize * i};
			Cell *enclosingCell = findEnclosingCell(rootCell, pixelPos);
			red = (unsigned char)(enclosingCell->cellIndex % 256);
			green = (unsigned char)(enclosingCell->cellIndex << 3 % 256);
			blue = (unsigned char)(enclosingCell->cellIndex << 1 % 256);
			alpha = (unsigned char)1.0;
			fprintf(file, "%d %d %d\n", red, green, blue);
		}
	}
	fclose(file);
	*/
}


void ruvmWriteRuvmFile(RuvmContext pContext, RuvmMesh *pMesh) {
	ByteString header;
	ByteString data;
	char *vertAttributes[VERT_ATTRIBUTE_AMOUNT];
	char *vertAttributeTypes[VERT_ATTRIBUTE_AMOUNT];
	int32_t vertAttributeSizes[VERT_ATTRIBUTE_AMOUNT];
	vertAttributes[0] = "position.x";
	vertAttributeTypes[0] = "f";
	vertAttributeSizes[0] = 32;
	vertAttributes[1] = "position.y";
	vertAttributeTypes[1] = "f";
	vertAttributeSizes[1] = 32;
	vertAttributes[2] = "position.z";
	vertAttributeTypes[2] = "f";
	vertAttributeSizes[2] = 32;
	char *loopAttributes[LOOP_ATTRIBUTE_AMOUNT];
	char *loopAttributeTypes[LOOP_ATTRIBUTE_AMOUNT];
	int32_t loopAttributeSizes[LOOP_ATTRIBUTE_AMOUNT];
	loopAttributes[0] = "normal.x";
	loopAttributeTypes[0] = "f";
	loopAttributeSizes[0] = 32;
	loopAttributes[1] = "normal.y";
	loopAttributeTypes[1] = "f";
	loopAttributeSizes[1] = 32;
	loopAttributes[2] = "normal.z";
	loopAttributeTypes[2] = "f";
	loopAttributeSizes[2] = 32;
	/*
	loopAttributes[3] = "uv.u";
	loopAttributeTypes[3] = "f";
	loopAttributeSizes[3] = 32;
	loopAttributes[4] = "uv.v";
	loopAttributeTypes[4] = "f";
	loopAttributeSizes[4] = 32;
	*/
	int32_t headerAttributesByteSize = 0;
	int64_t vertAttributeByteSize = 0;
	for (int32_t i = 0; i < VERT_ATTRIBUTE_AMOUNT; ++i) {
		headerAttributesByteSize += strlen(vertAttributes[i]) + 1;
		headerAttributesByteSize += strlen(vertAttributeTypes[i]) + 1;
		headerAttributesByteSize += 1;
		vertAttributeByteSize += vertAttributeSizes[i];
	}
	int64_t loopAttributeByteSize = 0;
	for (int32_t i = 0; i < LOOP_ATTRIBUTE_AMOUNT; ++i) {
		headerAttributesByteSize += strlen(loopAttributes[i]) + 1;
		headerAttributesByteSize += strlen(loopAttributeTypes[i]) + 1;
		headerAttributesByteSize += 1;
		loopAttributeByteSize += loopAttributeSizes[i];
	}
	int64_t headerSizeInBits = 32 +
	                             32 +
	                             headerAttributesByteSize * 8 +
	                             32 +
	                             32 +
	                             32;
	int64_t dataSizeInBits = vertAttributeByteSize * 8l * (int64_t)pMesh->vertCount +
	                           (32l + loopAttributeByteSize * 8l) * pMesh->loopCount +
	                           32l * pMesh->faceCount;
	int32_t headerSizeInBytes = (int32_t)(headerSizeInBits / 8 + 2);
	int64_t dataSizeInBytes = (int64_t)(dataSizeInBits / 8l + 2l);
	data.byteIndex = 0;
	data.nextBitIndex = 0;
	header.byteIndex = 0;
	header.nextBitIndex = 0;

	data.pString = pContext->alloc.pCalloc(dataSizeInBytes, 8);
	for (int32_t i = 0; i < pMesh->vertCount; ++i) {
		encodeValue(&data, (uint8_t *)&pMesh->pVerts[i].x, 32);
		encodeValue(&data, (uint8_t *)&pMesh->pVerts[i].y, 32);
		encodeValue(&data, (uint8_t *)&pMesh->pVerts[i].z, 32);
	}
	for (int32_t i = 0; i < pMesh->loopCount; ++i) {
		encodeValue(&data, (uint8_t *)&pMesh->pLoops[i], 32);
	}
	for (int32_t i = 0; i < pMesh->loopCount; ++i) {
		encodeValue(&data, (uint8_t *)&pMesh->pNormals[i].x, 32);
		encodeValue(&data, (uint8_t *)&pMesh->pNormals[i].y, 32);
		encodeValue(&data, (uint8_t *)&pMesh->pNormals[i].z, 32);
	}
	/*
	for (int32_t i = 0; i < pMesh->loopCount; ++i) {
		encodeValue(&data, (uint8_t *)&pMesh->pUvs[i].x, 32);
		encodeValue(&data, (uint8_t *)&pMesh->pUvs[i].y, 32);
	}
	*/
	for (int32_t i = 0; i < pMesh->faceCount; ++i) {
		encodeValue(&data, (uint8_t *)&pMesh->pFaces[i], 32);
	}

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

	header.pString = pContext->alloc.pCalloc(headerSizeInBytes, sizeof(uint8_t));
	encodeValue(&header, (uint8_t *)&compressedDataSize, 32);
	encodeValue(&header, (uint8_t *)&dataSize, 32);
	int32_t vertAttributeSize = VERT_ATTRIBUTE_AMOUNT;
	encodeValue(&header, (uint8_t *)&vertAttributeSize, 8);
	for (int32_t i = 0; i < VERT_ATTRIBUTE_AMOUNT; ++i) {
		encodeString(&header, (uint8_t *)vertAttributes[i], (strlen(vertAttributes[i]) + 1) * 8);
		encodeString(&header, (uint8_t *)vertAttributeTypes[i], (strlen(vertAttributeTypes[i]) + 1) * 8);
		encodeValue(&header, (uint8_t *)&vertAttributeSizes[i], 8);
	}
	int32_t loopAttributeSize = LOOP_ATTRIBUTE_AMOUNT;
	encodeValue(&header, (uint8_t *)&loopAttributeSize, 8);
	for (int32_t i = 0; i < LOOP_ATTRIBUTE_AMOUNT; ++i) {
		encodeString(&header, (uint8_t *)loopAttributes[i], (strlen(loopAttributes[i]) + 1) * 8);
		encodeString(&header, (uint8_t *)loopAttributeTypes[i], (strlen(loopAttributeTypes[i]) + 1) * 8);
		encodeValue(&header, (uint8_t *)&loopAttributeSizes[i], 8);
	}
	encodeValue(&header, (uint8_t *)&pMesh->vertCount, 32);
	encodeValue(&header, (uint8_t *)&pMesh->loopCount, 32);
	encodeValue(&header, (uint8_t *)&pMesh->faceCount, 32);

	// CRC for uncompressed data, not compressed!
	
	void *pFile;
	pContext->io.pOpen(&pFile, "T:/workshop_folders/RUVM/TestOutputDir/File.ruvm",
	                   0, &pContext->alloc);
	pContext->io.pWrite(pFile, (uint8_t *)&headerSizeInBytes, 4);
	pContext->io.pWrite(pFile, header.pString, header.byteIndex + (header.nextBitIndex > 0));
	pContext->io.pWrite(pFile, compressedData, (int32_t)compressedDataSize);
	pContext->io.pClose(pFile);

	free(data.pString);

	printf("Finished RUVM export\n");
}

static void decodeRuvmHeader(RuvmContext pContext, RuvmMap pMapFile, ByteString *headerByteString) {
	RuvmHeader *header = &pMapFile->header;
	RuvmMesh *pMesh = &pMapFile->mesh;
	//printf("0\n");
	decodeValue(headerByteString, (uint8_t *)&header->dataSizeCompressed, 32);
	//printf("dataSizeCompressed %d\n", header->dataSizeCompressed);
	decodeValue(headerByteString, (uint8_t *)&header->dataSize, 32);
	//printf("dataSize %d\n", header->dataSize);
	decodeValue(headerByteString, (uint8_t *)&header->vertAttributeSize, 8);
	//printf("vertAttributeSize %d\n", header->vertAttributeSize);
	header->pVertAttributeDesc = pContext->alloc.pCalloc(header->vertAttributeSize, sizeof(AttributeDesc));
	//printf("1\n");
	for (int32_t i = 0; i < header->vertAttributeSize; ++i) {
		decodeString(headerByteString, header->pVertAttributeDesc[i].name, 64);
		//printf("vertAttributte[%d] name %s\n", i, header->vertAttributeDesc[i].name);
		decodeString(headerByteString, header->pVertAttributeDesc[i].type, 2);
		//printf("vertAttributte[%d] type %s\n", i, header->vertAttributeDesc[i].type);
		decodeValue(headerByteString, (uint8_t *)&header->pVertAttributeDesc[i].sizeInBits, 8);
		//printf("vertAttributte[%d] size %d\n", i, header->vertAttributeDesc[i].sizeInBits);
	}
	printf("2\n");
	decodeValue(headerByteString, (uint8_t *)&header->loopAttributeSize, 8);
	printf("loopAttributeSize %d\n", header->loopAttributeSize);
	header->pLoopAttributeDesc = pContext->alloc.pCalloc(header->loopAttributeSize, sizeof(AttributeDesc));
	printf("3\n");
	for (int32_t i = 0; i < header->loopAttributeSize; ++i) {
		printf("3.1  iteration: %d\n", i);
		decodeString(headerByteString, header->pLoopAttributeDesc[i].name, 64);
		decodeString(headerByteString, header->pLoopAttributeDesc[i].type, 2);
		decodeValue(headerByteString, (uint8_t *)&header->pLoopAttributeDesc[i].sizeInBits, 8);
	}
	printf("4\n");
	decodeValue(headerByteString, (uint8_t *)&pMesh->vertCount, 32);
	decodeValue(headerByteString, (uint8_t *)&pMesh->loopCount, 32);
	printf("Byte size before overflow: %d\n", headerByteString->byteIndex);
	decodeValue(headerByteString, (uint8_t *)&pMesh->faceCount, 32);
	printf("5\n");

}

static void decodeRuvmData(RuvmContext pContext, RuvmMap pMapFile, ByteString *dataByteString, int32_t getUvs) {
	Mesh *pMesh = &pMapFile->mesh;
	pMesh->pVerts = pContext->alloc.pCalloc(pMesh->vertCount, sizeof(Vec3));
	for (int32_t i = 0; i < pMesh->vertCount; ++i) {
		decodeValue(dataByteString, (uint8_t *)&pMesh->pVerts[i].x, 32);
		decodeValue(dataByteString, (uint8_t *)&pMesh->pVerts[i].y, 32);
		decodeValue(dataByteString, (uint8_t *)&pMesh->pVerts[i].z, 32);
	}
	pMesh->pLoops = pContext->alloc.pCalloc(pMesh->loopCount, sizeof(int32_t));
	for (int32_t i = 0; i < pMesh->loopCount; ++i) {
		decodeValue(dataByteString, (uint8_t *)&pMesh->pLoops[i], 32);
	}
	pMesh->pNormals = pContext->alloc.pCalloc(pMesh->loopCount, sizeof(Vec3));
	for (int32_t i = 0; i < pMesh->loopCount; ++i) {
		decodeValue(dataByteString, (uint8_t *)&pMesh->pNormals[i].x, 32);
		decodeValue(dataByteString, (uint8_t *)&pMesh->pNormals[i].y, 32);
		decodeValue(dataByteString, (uint8_t *)&pMesh->pNormals[i].z, 32);
	}
	if (getUvs == 1) {
		pMesh->pUvs = pContext->alloc.pCalloc(pMesh->loopCount, sizeof(Vec2));
		for (int32_t i = 0; i < pMesh->loopCount; ++i) {
			decodeValue(dataByteString, (uint8_t *)&pMesh->pUvs[i].x, 32);
			decodeValue(dataByteString, (uint8_t *)&pMesh->pUvs[i].y, 32);
		}
	}
	// + 1 because blender stores an extra at end, so that number of loops can be
	// checked with faceBuffer[i + 1] - faceBuffer[i], without causing a crash.
	pMesh->pFaces = pContext->alloc.pCalloc(pMesh->faceCount + 1, sizeof(int32_t));
	for (int32_t i = 0; i < pMesh->faceCount; ++i) {
		decodeValue(dataByteString, (uint8_t *)&pMesh->pFaces[i], 32);
	}
	pMesh->pFaces[pMesh->faceCount] = pMesh->loopCount;
}

void ruvmLoadRuvmFile(RuvmContext pContext, RuvmMap pMapFile, char *filePath, int32_t getUvs) {
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
	decodeRuvmHeader(pContext, pMapFile, &headerByteString);
	uint8_t *dataByteStringRaw = pContext->alloc.pMalloc(pMapFile->header.dataSize);
	unsigned long dataSizeUncompressed = pMapFile->header.dataSize;
	printf("Reading data\n");
	pContext->io.pRead(pFile, dataByteStringRaw, pMapFile->header.dataSizeCompressed);
	pContext->io.pClose(pFile);
	dataByteString.pString = pContext->alloc.pMalloc(pMapFile->header.dataSize);
	printf("Decompressing data\n");
	int32_t zResult = uncompress(dataByteString.pString, &dataSizeUncompressed, dataByteStringRaw, pMapFile->header.dataSizeCompressed);
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
	if (dataSizeUncompressed != pMapFile->header.dataSize) {
		printf("Failed to load RUVM file. Decompressed data size doesn't match header description\n");
		return;
	}
	printf("Decoding data\n");
	decodeRuvmData(pContext, pMapFile, &dataByteString, getUvs);
	pContext->alloc.pFree(headerByteString.pString);
	pContext->alloc.pFree(dataByteString.pString);
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
