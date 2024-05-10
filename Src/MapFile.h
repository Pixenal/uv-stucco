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
	int32_t version;
	int64_t dataSize;
	int64_t dataSizeCompressed;
} RuvmHeader;

typedef struct RuvmMapInternal {
	Mesh mesh;
	QuadTree quadTree;
	V2_F32 zBounds;
} MapFile;
