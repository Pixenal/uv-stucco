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
} UvgpData;

typedef struct {
	UvgpHeader header;
	UvgpData data;
	QuadTree quadTree;
} UvgpFileLoaded;

void encodeValue(UvgpByteString *data, unsigned char *value, int32_t lengthInBits);
void encodeString(UvgpByteString *data, unsigned char *value, int32_t lengthInBits);
void decodeValue(UvgpByteString *byteString, unsigned char *value, int32_t lengthInBits);
void decodeString(UvgpByteString *byteString, char *string, int32_t stringLength);
void writeDebugImage(Cell *rootCell);
void writeUvgpFile(int32_t vertAmount, float *vertBuffer, int32_t loopAmount, int32_t *loopBuffer, int32_t faceAmount, int32_t *faceBuffer);
void decodeUvgpHeader(UvgpFileLoaded *fileLoaded, UvgpByteString *headerByteString);
void loadUvgpFile(UvgpFileLoaded *fileLoaded, char *filePath);
void destroyUvgpFile(UvgpFileLoaded *fileLoaded);
