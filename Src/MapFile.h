#pragma once
#include <stdint.h>
#include <QuadTree.h>
#include <RUVM.h>

typedef struct {
	char name[64];
	char type[2];
	int32_t sizeInBits;
} AttributeDesc;

typedef struct {
	int32_t dataSize;
	int32_t dataSizeCompressed;
	int32_t vertAttributeSize;
	AttributeDesc *pVertAttributeDesc;
	int32_t loopAttributeSize;
	AttributeDesc *pLoopAttributeDesc;
} RuvmHeader;

typedef struct RuvmMapInternal {
	RuvmHeader header;
	RuvmMesh mesh;
	QuadTree quadTree;
} MapFile;
