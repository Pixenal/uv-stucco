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
extern int32_t leafAmount;

int32_t decodeSingleBit(UvgpByteString *byteString) {
	int32_t value = byteString->string[byteString->byteIndex];
	value >>= byteString->nextBitIndex;
	value &= 1;
	byteString->nextBitIndex++;
	byteString->byteIndex += byteString->nextBitIndex >= 8;
	byteString->nextBitIndex %= 8;
	return value;
}

void encodeValue(UvgpByteString *byteString, unsigned char *value, int32_t lengthInBits) {
	unsigned char valueBuffer[10] = {0};
	int32_t lengthInBytes = lengthInBits / 8;
	lengthInBytes += (lengthInBits - lengthInBytes * 8) > 0;
	for (int32_t i = 1; i <= lengthInBytes; ++i) {
		valueBuffer[i] = value[i - 1];
	}
	for (int32_t i = 7; i >= 1; --i) {
		valueBuffer[i] <<= byteString->nextBitIndex;
		unsigned char nextByteCopy = valueBuffer[i - 1];
		nextByteCopy >>= 8 - byteString->nextBitIndex;
		valueBuffer[i] |= nextByteCopy;
	}
	int32_t writeUpTo = lengthInBytes + (byteString->nextBitIndex > 0);
	for (int32_t i = 0; i < writeUpTo; ++i) {
		byteString->string[byteString->byteIndex + i] |= valueBuffer[i + 1];
	}
	byteString->nextBitIndex = byteString->nextBitIndex + lengthInBits;
	byteString->byteIndex += byteString->nextBitIndex / 8;
	byteString->nextBitIndex %= 8;
}

void encodeString(UvgpByteString *byteString, unsigned char *string, int32_t lengthInBits) {
	int32_t lengthInBytes = lengthInBits / 8;
	byteString->byteIndex += byteString->nextBitIndex > 0;
	byteString->nextBitIndex = 0;
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		byteString->string[byteString->byteIndex] = string[i];
		byteString->byteIndex++;
	}
}

void decodeValue(UvgpByteString *byteString, unsigned char *value, int32_t lengthInBits) {
	int32_t lengthInBytes = lengthInBits / 8;
	int32_t bitDifference = lengthInBits - lengthInBytes * 8;
	lengthInBytes += bitDifference > 0;
	unsigned char buffer[ENCODE_DECODE_BUFFER_LENGTH] = {0};
	for (int32_t i = 0; i < ENCODE_DECODE_BUFFER_LENGTH; ++i) {
		buffer[i] = byteString->string[byteString->byteIndex + i];
	}
	for (int32_t i = 0; i < ENCODE_DECODE_BUFFER_LENGTH; ++i) {
		buffer[i] >>= byteString->nextBitIndex;
		unsigned char nextByteCopy = buffer[i + 1];
		nextByteCopy <<= 8 - byteString->nextBitIndex;
		buffer[i] |= nextByteCopy;
	}
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		value[i] |= buffer[i];
	}
	unsigned char mask = UCHAR_MAX >> ((8 - bitDifference) % 8);
	value[lengthInBytes - 1] &= mask;
	byteString->nextBitIndex = byteString->nextBitIndex + lengthInBits;
	byteString->byteIndex += byteString->nextBitIndex / 8;
	byteString->nextBitIndex %= 8;
}

void decodeString(UvgpByteString *byteString, char *string, int32_t stringLength) {
	char *stringCopy = string;
	byteString->byteIndex += byteString->nextBitIndex > 0;
	byteString->nextBitIndex = 0;
	unsigned char *dataPtr = byteString->string + byteString->byteIndex;
	char *lastChar = string + (stringLength - 1);
	*lastChar = 1;
	do {
		*string = *dataPtr;
	} while(*++dataPtr && !*++string);
	*lastChar = 0;
	byteString->byteIndex += stringLength - (lastChar - string) + 1;
}

void writeDebugImage(Cell *rootCell) {
	FILE* file;
	file = fopen("/run/media/calebdawson/Tuna/workshop_folders/UVGP/DebugOutput_LoadedFile.ppm", "w");
	fprintf(file, "P3\n%d %d\n255\n", 512, 512);
	for (int32_t i = 0; i < 512; ++i) {
		for (int32_t j = 0; j < 512; ++j) {
			unsigned char red, green, blue, alpha;
			int32_t linearIndex = (i * 512) + j;
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


void writeUvgpFile(int32_t vertAmount, float *vertBuffer, int32_t loopAmount, int32_t *loopBuffer, int32_t faceAmount, int32_t *faceBuffer) {
	UvgpByteString header;
	UvgpByteString data;
	char *vertAttributes[VERT_ATTRIBUTE_AMOUNT];
	char *vertAttributeTypes[VERT_ATTRIBUTE_AMOUNT];
	int32_t vertAttributeSize[VERT_ATTRIBUTE_AMOUNT];
	vertAttributes[0] = "position.x";
	vertAttributeTypes[0] = "f";
	vertAttributeSize[0] = 32;
	vertAttributes[1] = "position.y";
	vertAttributeTypes[1] = "f";
	vertAttributeSize[1] = 32;
	vertAttributes[2] = "position.z";
	vertAttributeTypes[2] = "f";
	vertAttributeSize[2] = 32;
	char *loopAttributes[LOOP_ATTRIBUTE_AMOUNT];
	char *loopAttributeTypes[LOOP_ATTRIBUTE_AMOUNT];
	int32_t loopAttributeSize[LOOP_ATTRIBUTE_AMOUNT];
	loopAttributes[0] = "normal.x";
	loopAttributeTypes[0] = "f";
	loopAttributeSize[0] = 32;
	loopAttributes[1] = "normal.y";
	loopAttributeTypes[1] = "f";
	loopAttributeSize[1] = 32;
	loopAttributes[2] = "normal.z";
	loopAttributeTypes[2] = "f";
	loopAttributeSize[2] = 32;
	int32_t headerAttributesByteLength = 0;
	int32_t vertAttributeByteLength = 0;
	for (int32_t i = 0; i < VERT_ATTRIBUTE_AMOUNT; ++i) {
		headerAttributesByteLength += strlen(vertAttributes[i]) + 1;
		headerAttributesByteLength += strlen(vertAttributeTypes[i]) + 1;
		headerAttributesByteLength += 1;
		vertAttributeByteLength += vertAttributeSize[i];
	}
	int32_t loopAttributeByteLength = 0;
	for (int32_t i = 0; i < LOOP_ATTRIBUTE_AMOUNT; ++i) {
		headerAttributesByteLength += strlen(loopAttributes[i]) + 1;
		headerAttributesByteLength += strlen(loopAttributeTypes[i]) + 1;
		headerAttributesByteLength += 1;
		loopAttributeByteLength += loopAttributeSize[i];
	}
	int64_t headerLengthInBits = 32 +
	                             32 +
	                             headerAttributesByteLength * 8 +
	                             32 +
	                             32 +
	                             32;
	int64_t dataLengthInBits = vertAttributeByteLength * 8 * vertAmount +
	                           2 + (32 + loopAttributeByteLength * 8) * 4 * faceAmount +
	                           cellIndex +
	                           32 * leafAmount +
	                           32 * vertAmount;
	int32_t headerLengthInBytes = (int32_t)(headerLengthInBits / 8 + 2);
	int32_t dataLengthInBytes = (int32_t)(dataLengthInBits / 8 + 2);
	data.byteIndex = 0;
	data.nextBitIndex = 0;
	header.byteIndex = 0;
	header.nextBitIndex = 0;

	data.string = calloc(dataLengthInBytes, sizeof(unsigned char));
	for (int32_t i = 0; i < vertAmount; i += 3) {
		encodeValue(&data, (unsigned char *)&vertBuffer[i], 32);
		encodeValue(&data, (unsigned char *)&vertBuffer[i + 1], 32);
		encodeValue(&data, (unsigned char *)&vertBuffer[i + 2], 32);
	}
	/*
	for (int32_t i = 0; i < faceAmount; ++i) {
		encodeValue(&data, (unsigned char *)&faceBuffer[i].loopAmount, 2);
		for (int32_t j = 0; j < faceBuffer[i].loopAmount; ++j) {
			encodeValue(&data, (unsigned char *)&faceBuffer[i].loops[j].vert, 32);
			encodeValue(&data, (unsigned char *)&faceBuffer[i].loops[j].normal.x, 32);
			encodeValue(&data, (unsigned char *)&faceBuffer[i].loops[j].normal.y, 32);
			encodeValue(&data, (unsigned char *)&faceBuffer[i].loops[j].normal.z, 32);
		}
	}
	*/

	int64_t dataLength = data.byteIndex + (data.nextBitIndex > 0);
	int64_t dataLengthExtra = dataLength / 1000;
	dataLengthExtra += ((dataLength * 1000) - dataLength) > 0;
	dataLengthExtra += 12;
	unsigned long compressedDataLength = dataLength + dataLengthExtra;
	unsigned char *compressedData = malloc(compressedDataLength);
	int32_t zResult = compress(compressedData, &compressedDataLength, data.string, dataLength);
	switch(zResult) {
		case Z_OK:
			printf("Successfully compressed UVGP data\n");
			break;
		case Z_MEM_ERROR:
			printf("Failed to compress UVGP data, memory error\n");
			break;
		case Z_BUF_ERROR:
			printf("Failed to compress UVGP data, output buffer too small\n");
			break;
	}

	printf("Compressed data is %lu long\n", compressedDataLength);

	header.string = calloc(headerLengthInBytes, sizeof(unsigned char));
	encodeValue(&header, (unsigned char *)&compressedDataLength, 32);
	encodeValue(&header, (unsigned char *)&dataLength, 32);
	int32_t vertAttributeAmount = VERT_ATTRIBUTE_AMOUNT;
	encodeValue(&header, (unsigned char *)&vertAttributeAmount, 8);
	for (int32_t i = 0; i < VERT_ATTRIBUTE_AMOUNT; ++i) {
		encodeString(&header, (unsigned char *)vertAttributes[i], (strlen(vertAttributes[i]) + 1) * 8);
		encodeString(&header, (unsigned char *)vertAttributeTypes[i], (strlen(vertAttributeTypes[i]) + 1) * 8);
		encodeValue(&header, (unsigned char *)&vertAttributeSize[i], 8);
	}
	int32_t loopAttributeAmount = LOOP_ATTRIBUTE_AMOUNT;
	encodeValue(&header, (unsigned char *)&loopAttributeAmount, 8);
	for (int32_t i = 0; i < LOOP_ATTRIBUTE_AMOUNT; ++i) {
		encodeString(&header, (unsigned char *)loopAttributes[i], (strlen(loopAttributes[i]) + 1) * 8);
		encodeString(&header, (unsigned char *)loopAttributeTypes[i], (strlen(loopAttributeTypes[i]) + 1) * 8);
		encodeValue(&header, (unsigned char *)&loopAttributeSize[i], 8);
	}
	encodeValue(&header, (unsigned char *)&vertAmount, 32);
	encodeValue(&header, (unsigned char *)&faceAmount, 32);
	encodeValue(&header, (unsigned char *)&cellIndex, 32);

	// CRC for uncompressed data, not compressed!
	
	UvgpFile file;
	uvgpFileOpen(&file, "/run/media/calebdawson/Tuna/workshop_folders/UVGP/TestOutputDir/File.uvgp", 0);
	uvgpFileWrite(&file, (unsigned char *)&headerLengthInBytes, 4);
	uvgpFileWrite(&file, header.string, header.byteIndex + (header.nextBitIndex > 0));
	uvgpFileWrite(&file, compressedData, (int32_t)compressedDataLength);
	uvgpFileClose(&file);

	free(data.string);

	printf("Finished UVGP export\n");
}

void decodeUvgpHeader(UvgpFileLoaded *fileLoaded, UvgpByteString *headerByteString) {
	UvgpHeader *header = &fileLoaded->header;
	//printf("0\n");
	decodeValue(headerByteString, (unsigned char *)&header->dataLengthCompressed, 32);
	//printf("dataLengthCompressed %d\n", header->dataLengthCompressed);
	decodeValue(headerByteString, (unsigned char *)&header->dataLength, 32);
	//printf("dataLength %d\n", header->dataLength);
	decodeValue(headerByteString, (unsigned char *)&header->vertAttributeAmount, 8);
	//printf("vertAttributeAmount %d\n", header->vertAttributeAmount);
	header->vertAttributeDesc = calloc(header->vertAttributeAmount, sizeof(AttributeDesc));
	//printf("1\n");
	for (int32_t i = 0; i < header->vertAttributeAmount; ++i) {
		decodeString(headerByteString, header->vertAttributeDesc[i].name, 64);
		//printf("vertAttributte[%d] name %s\n", i, header->vertAttributeDesc[i].name);
		decodeString(headerByteString, header->vertAttributeDesc[i].type, 2);
		//printf("vertAttributte[%d] type %s\n", i, header->vertAttributeDesc[i].type);
		decodeValue(headerByteString, (unsigned char *)&header->vertAttributeDesc[i].sizeInBits, 8);
		//printf("vertAttributte[%d] size %d\n", i, header->vertAttributeDesc[i].sizeInBits);
	}
	printf("2\n");
	decodeValue(headerByteString, (unsigned char *)&header->loopAttributeAmount, 8);
	printf("loopAttributeAmount %d\n", header->loopAttributeAmount);
	header->loopAttributeDesc = calloc(header->loopAttributeAmount, sizeof(AttributeDesc));
	printf("3\n");
	for (int32_t i = 0; i < header->loopAttributeAmount; ++i) {
		printf("3.1  iteration: %d\n", i);
		decodeString(headerByteString, header->loopAttributeDesc[i].name, 64);
		decodeString(headerByteString, header->loopAttributeDesc[i].type, 2);
		decodeValue(headerByteString, (unsigned char *)&header->loopAttributeDesc[i].sizeInBits, 8);
	}
	printf("4\n");
	decodeValue(headerByteString, (unsigned char *)&header->vertAmount, 32);
	decodeValue(headerByteString, (unsigned char *)&header->faceAmount, 32);
	decodeValue(headerByteString, (unsigned char *)&header->cellAmount, 32);
	printf("5\n");

}

void decodeUvgpData(UvgpFileLoaded *fileLoaded, UvgpByteString *dataByteString) {
	UvgpHeader *header = &fileLoaded->header;
	UvgpData *data = &fileLoaded->data;
	data->vertBuffer = malloc(sizeof(Vert) * header->vertAmount);
	for (int32_t i = 0; i < header->vertAmount; ++i) {
		decodeValue(dataByteString, (unsigned char *)&data->vertBuffer[i].pos.x, 32);
		decodeValue(dataByteString, (unsigned char *)&data->vertBuffer[i].pos.y, 32);
		decodeValue(dataByteString, (unsigned char *)&data->vertBuffer[i].pos.z, 32);
	}
	data->faceBuffer = malloc(sizeof(Face) * header->faceAmount);
	for (int32_t i = 0; i < header->faceAmount; ++i) {
		Face *face = data->faceBuffer + i;
		decodeValue(dataByteString, (unsigned char *)&face->loopAmount, 2);
		for (int32_t j = 0; j < face->loopAmount; ++j) {
			decodeValue(dataByteString, (unsigned char *)&face->loops[j].vert, 32);
			decodeValue(dataByteString, (unsigned char *)&face->loops[j].normal.x , 32);
			decodeValue(dataByteString, (unsigned char *)&face->loops[j].normal.y , 32);
			decodeValue(dataByteString, (unsigned char *)&face->loops[j].normal.z , 32);
		}
	}
}

void loadUvgpFile(UvgpFileLoaded *fileLoaded, char *filePath) {
	UvgpByteString headerByteString = {0};
	UvgpByteString dataByteString = {0};
	UvgpFile file;
	printf("Loading UVGP file: %s\n", filePath);
	uvgpFileOpen(&file, filePath, 1);
	unsigned char *headerLength = malloc(4);
	uvgpFileRead(&file, headerLength, 4);
	int32_t headerLengthInt = *((int32_t *)headerLength);
	printf("Header is %d bytes\n", headerLengthInt);
	headerByteString.string = malloc(headerLengthInt);
	printf("Reading header\n");
	uvgpFileRead(&file, headerByteString.string, headerLengthInt);
	printf("Decoding header\n");
	decodeUvgpHeader(fileLoaded, &headerByteString);
	unsigned char *dataByteStringRaw = malloc(fileLoaded->header.dataLength);
	unsigned long dataLengthUncompressed = fileLoaded->header.dataLength;
	printf("Reading data\n");
	uvgpFileRead(&file, dataByteStringRaw, fileLoaded->header.dataLengthCompressed);
	dataByteString.string = malloc(fileLoaded->header.dataLength);
	printf("Decompressing data\n");
	int32_t zResult = uncompress(dataByteString.string, &dataLengthUncompressed, dataByteStringRaw, fileLoaded->header.dataLengthCompressed);
	switch(zResult) {
		case Z_OK:
			printf("Successfully decompressed UVGP file data\n");
			break;
		case Z_MEM_ERROR:
			printf("Failed to decompress UVGP file data. Memory error\n");
			break;
		case Z_BUF_ERROR:
			printf("Failed to decompress UVGP file data. Buffer was too small\n");
			break;
	}
	if (dataLengthUncompressed != fileLoaded->header.dataLength) {
		printf("Failed to load UVGP file. Decompressed data size doesn't match header description\n");
		return;
	}
	printf("Decoding data\n");
	decodeUvgpData(fileLoaded, &dataByteString);
}

void destroyUvgpFile(UvgpFileLoaded *fileLoaded) {
	free(fileLoaded->data.vertBuffer);
	free(fileLoaded->data.faceBuffer);
	free(fileLoaded);
}
