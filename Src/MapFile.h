#pragma once
#include <stdint.h>
#include <QuadTree.h>
#include <RUVM.h>
#include <Usg.h>

typedef struct {
	char name[64];
	char type[2];
	int32_t sizeInBits;
} AttributeDesc;

typedef struct {
	char format[14];
	int32_t version;
	int64_t dataSize;
	int64_t dataSizeCompressed;
	int32_t objCount;
	int32_t usgCount;
	int32_t flatCutoffCount;
} RuvmHeader;

typedef struct RuvmMapInternal {
	Mesh mesh;
	QuadTree quadTree;
	UsgArr usgArr;
	V2_F32 zBounds;
	RuvmAttribIndexedArr indexedAttribs;
} MapFile;
