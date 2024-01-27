#define VERT_ATTRIBUTE_AMOUNT 3
#define LOOP_ATTRIBUTE_AMOUNT 3
#define ENCODE_DECODE_BUFFER_LENGTH 5

#include "Io.h"
#include "QuadTree.h"
#include <stdbool.h>
#include <stdlib.h>
#include <limits.h>
#include "miniz.h"
#include "Platform.h"

extern int32_t cellIndex;
extern int32_t leafSize;

typedef struct {
	unsigned char *pString;
	int32_t nextBitIndex;
	int32_t byteIndex;
} ByteString;

int32_t decodeSingleBit(ByteString *byteString) {
	int32_t value = byteString->pString[byteString->byteIndex];
	value >>= byteString->nextBitIndex;
	value &= 1;
	byteString->nextBitIndex++;
	byteString->byteIndex += byteString->nextBitIndex >= 8;
	byteString->nextBitIndex %= 8;
	return value;
}

void encodeValue(ByteString *byteString, uint8_t *value, int32_t lengthInBits) {
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

void encodeString(ByteString *byteString, uint8_t *string, int32_t lengthInBits) {
	int32_t lengthInBytes = lengthInBits / 8;
	byteString->byteIndex += byteString->nextBitIndex > 0;
	byteString->nextBitIndex = 0;
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		byteString->pString[byteString->byteIndex] = string[i];
		byteString->byteIndex++;
	}
}

void decodeValue(ByteString *byteString, uint8_t *value, int32_t lengthInBits) {
	int32_t lengthInBytes = lengthInBits / 8;
	int32_t bitDifference = lengthInBits - lengthInBytes * 8;
	lengthInBytes += bitDifference > 0;
	uint8_t buffer[ENCODE_DECODE_BUFFER_LENGTH] = {0};
	for (int32_t i = 0; i < ENCODE_DECODE_BUFFER_LENGTH; ++i) {
		buffer[i] = byteString->pString[byteString->byteIndex + i];
	}
	for (int32_t i = 0; i < ENCODE_DECODE_BUFFER_LENGTH; ++i) {
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

void decodeString(ByteString *byteString, char *string, int32_t stringSize) {
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

void writeDebugImage(Cell *rootCell) {
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
}


void writeRuvmFile(MeshData *pMesh) {
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
	int32_t headerAttributesByteSize = 0;
	int32_t vertAttributeByteSize = 0;
	for (int32_t i = 0; i < VERT_ATTRIBUTE_AMOUNT; ++i) {
		headerAttributesByteSize += strlen(vertAttributes[i]) + 1;
		headerAttributesByteSize += strlen(vertAttributeTypes[i]) + 1;
		headerAttributesByteSize += 1;
		vertAttributeByteSize += vertAttributeSizes[i];
	}
	int32_t loopAttributeByteSize = 0;
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
	int64_t dataSizeInBits = vertAttributeByteSize * 8 * pMesh->vertSize +
	                           2 + (32 + loopAttributeByteSize * 8) * 4 * pMesh->faceSize +
	                           cellIndex +
	                           32 * leafSize +
	                           32 * pMesh->vertSize;
	int32_t headerSizeInBytes = (int32_t)(headerSizeInBits / 8 + 2);
	int32_t dataSizeInBytes = (int32_t)(dataSizeInBits / 8 + 2);
	data.byteIndex = 0;
	data.nextBitIndex = 0;
	header.byteIndex = 0;
	header.nextBitIndex = 0;

	data.pString = calloc(dataSizeInBytes, 8);
	for (int32_t i = 0; i < pMesh->vertSize; ++i) {
		encodeValue(&data, (uint8_t *)&pMesh->pVerts[i].x, 32);
		encodeValue(&data, (uint8_t *)&pMesh->pVerts[i].y, 32);
		encodeValue(&data, (uint8_t *)&pMesh->pVerts[i].z, 32);
	}
	for (int32_t i = 0; i < pMesh->loopSize; ++i) {
		encodeValue(&data, (uint8_t *)&pMesh->pLoops[i], 32);
	}
	for (int32_t i = 0; i < pMesh->loopSize; ++i) {
		encodeValue(&data, (uint8_t *)&pMesh->pNormals[i].x, 32);
		encodeValue(&data, (uint8_t *)&pMesh->pNormals[i].y, 32);
		encodeValue(&data, (uint8_t *)&pMesh->pNormals[i].z, 32);
	}
	for (int32_t i = 0; i < pMesh->faceSize; ++i) {
		encodeValue(&data, (uint8_t *)&pMesh->pFaces[i], 32);
	}

	int64_t dataSize = data.byteIndex + (data.nextBitIndex > 0);
	int64_t dataSizeExtra = dataSize / 1000;
	dataSizeExtra += ((dataSize * 1000) - dataSize) > 0;
	dataSizeExtra += 12;
	unsigned long compressedDataSize = dataSize + dataSizeExtra;
	uint8_t *compressedData = malloc(compressedDataSize);
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

	header.pString = calloc(headerSizeInBytes, sizeof(uint8_t));
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
	encodeValue(&header, (uint8_t *)&pMesh->vertSize, 32);
	encodeValue(&header, (uint8_t *)&pMesh->loopSize, 32);
	encodeValue(&header, (uint8_t *)&pMesh->faceSize, 32);

	// CRC for uncompressed data, not compressed!
	
	PlatformFile file;
	platformFileOpen(&file, "/run/media/calebdawson/Tuna/workshop_folders/RUVM/TestOutputDir/File.ruvm", 0);
	platformFileWrite(&file, (uint8_t *)&headerSizeInBytes, 4);
	platformFileWrite(&file, header.pString, header.byteIndex + (header.nextBitIndex > 0));
	platformFileWrite(&file, compressedData, (int32_t)compressedDataSize);
	platformFileClose(&file);

	free(data.pString);

	printf("Finished RUVM export\n");
}

void decodeRuvmHeader(RuvmFileLoaded *pFileLoaded, ByteString *headerByteString) {
	RuvmHeader *header = &pFileLoaded->header;
	MeshData *pMesh = &pFileLoaded->mesh;
	//printf("0\n");
	decodeValue(headerByteString, (uint8_t *)&header->dataSizeCompressed, 32);
	//printf("dataSizeCompressed %d\n", header->dataSizeCompressed);
	decodeValue(headerByteString, (uint8_t *)&header->dataSize, 32);
	//printf("dataSize %d\n", header->dataSize);
	decodeValue(headerByteString, (uint8_t *)&header->vertAttributeSize, 8);
	//printf("vertAttributeSize %d\n", header->vertAttributeSize);
	header->pVertAttributeDesc = calloc(header->vertAttributeSize, sizeof(AttributeDesc));
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
	header->pLoopAttributeDesc = calloc(header->loopAttributeSize, sizeof(AttributeDesc));
	printf("3\n");
	for (int32_t i = 0; i < header->loopAttributeSize; ++i) {
		printf("3.1  iteration: %d\n", i);
		decodeString(headerByteString, header->pLoopAttributeDesc[i].name, 64);
		decodeString(headerByteString, header->pLoopAttributeDesc[i].type, 2);
		decodeValue(headerByteString, (uint8_t *)&header->pLoopAttributeDesc[i].sizeInBits, 8);
	}
	printf("4\n");
	decodeValue(headerByteString, (uint8_t *)&pMesh->vertSize, 32);
	decodeValue(headerByteString, (uint8_t *)&pMesh->loopSize, 32);
	decodeValue(headerByteString, (uint8_t *)&pMesh->faceSize, 32);
	printf("5\n");

}

void decodeRuvmData(RuvmFileLoaded *pFileLoaded, ByteString *dataByteString) {
	MeshData *pMesh = &pFileLoaded->mesh;
	pMesh->pVerts = calloc(pMesh->vertSize, sizeof(Vec3));
	for (int32_t i = 0; i < pMesh->vertSize; ++i) {
		decodeValue(dataByteString, (uint8_t *)&pMesh->pVerts[i].x, 32);
		decodeValue(dataByteString, (uint8_t *)&pMesh->pVerts[i].y, 32);
		decodeValue(dataByteString, (uint8_t *)&pMesh->pVerts[i].z, 32);
	}
	pMesh->pLoops = calloc(pMesh->loopSize, sizeof(int32_t));
	for (int32_t i = 0; i < pMesh->loopSize; ++i) {
		decodeValue(dataByteString, (uint8_t *)&pMesh->pLoops[i], 32);
	}
	pMesh->pNormals = calloc(pMesh->loopSize, sizeof(Vec3));
	for (int32_t i = 0; i < pMesh->loopSize; ++i) {
		decodeValue(dataByteString, (uint8_t *)&pMesh->pNormals[i].x, 32);
		decodeValue(dataByteString, (uint8_t *)&pMesh->pNormals[i].y, 32);
		decodeValue(dataByteString, (uint8_t *)&pMesh->pNormals[i].z, 32);
	}
	// + 1 because blender stores an extra at end, so that number of loops can be
	// checked with faceBuffer[i + 1] - faceBuffer[i], without causing a crash.
	pMesh->pFaces = calloc(pMesh->faceSize + 1, sizeof(int32_t));
	for (int32_t i = 0; i < pMesh->faceSize; ++i) {
		decodeValue(dataByteString, (uint8_t *)&pMesh->pFaces[i], 32);
	}
	pMesh->pFaces[pMesh->faceSize] = pMesh->loopSize;
}

void loadRuvmFile(RuvmFileLoaded *pFileLoaded, char *filePath) {
	ByteString headerByteString = {0};
	ByteString dataByteString = {0};
	PlatformFile file;
	printf("Loading RUVM file: %s\n", filePath);
	platformFileOpen(&file, filePath, 1);
	uint8_t *headerSize = malloc(4);
	platformFileRead(&file, headerSize, 4);
	int32_t headerSizeInt = *((int32_t *)headerSize);
	printf("Header is %d bytes\n", headerSizeInt);
	headerByteString.pString = malloc(headerSizeInt);
	printf("Reading header\n");
	platformFileRead(&file, headerByteString.pString, headerSizeInt);
	printf("Decoding header\n");
	decodeRuvmHeader(pFileLoaded, &headerByteString);
	uint8_t *dataByteStringRaw = malloc(pFileLoaded->header.dataSize);
	unsigned long dataSizeUncompressed = pFileLoaded->header.dataSize;
	printf("Reading data\n");
	platformFileRead(&file, dataByteStringRaw, pFileLoaded->header.dataSizeCompressed);
	dataByteString.pString = malloc(pFileLoaded->header.dataSize);
	printf("Decompressing data\n");
	int32_t zResult = uncompress(dataByteString.pString, &dataSizeUncompressed, dataByteStringRaw, pFileLoaded->header.dataSizeCompressed);
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
	if (dataSizeUncompressed != pFileLoaded->header.dataSize) {
		printf("Failed to load RUVM file. Decompressed data size doesn't match header description\n");
		return;
	}
	printf("Decoding data\n");
	decodeRuvmData(pFileLoaded, &dataByteString);
}

void destroyRuvmFile(RuvmFileLoaded *pFileLoaded) {
	free(pFileLoaded->mesh.pVerts);
	free(pFileLoaded->mesh.pFaces);
	free(pFileLoaded);
}
