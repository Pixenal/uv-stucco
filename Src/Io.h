#pragma once
#include "Types.h"
#include "QuadTree.h"
#include "Platform.h"

typedef struct {
	char name[64];
	char type[2];
	int32_t sizeInBits;
} AttributeDesc;

typedef struct {
	int32_t dataLength;
	int32_t dataLengthCompressed;
	int32_t vertAttributeAmount;
	AttributeDesc *vertAttributeDesc;
	int32_t loopAttributeAmount;
	AttributeDesc *loopAttributeDesc;
	int32_t vertAmount;
	int32_t faceAmount;
	int32_t cellAmount;
} UvgpHeader;

typedef struct {
	Vert *vertBuffer;
	Face *faceBuffer;
	Cell *rootCell;
} UvgpData;

typedef struct {
	UvgpHeader header;
	UvgpData data;
} UvgpFileLoaded;

void encodeValue(UvgpByteString *data, unsigned char *value, int32_t lengthInBits);
void encodeString(UvgpByteString *data, unsigned char *value, int32_t lengthInBits);
void decodeValue(UvgpByteString *byteString, unsigned char *value, int32_t lengthInBits);
void decodeString(UvgpByteString *byteString, char *string, int32_t stringLength);
void encodeDataAndDestroyQuadTree(Cell *rootCell, int32_t maxTreeDepth, UvgpByteString *data);
void writeDebugImage(Cell *rootCell);
void writeUvgpFileAndFreeMemory(Cell *rootCell, int32_t maxTreeDepth, int32_t vertAmount, Vert *vertBuffer, int32_t faceAmount, Face *faceBuffer);
void decodeUvgpHeader(UvgpFileLoaded *fileLoaded, UvgpByteString *headerByteString);
void readUvgpFile(UvgpFileLoaded *fileLoaded, UvgpByteString *headerByteString, UvgpByteString *dataByteString, char *filePath);
