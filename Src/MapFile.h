#pragma once
#include <stdint.h>

#include <Io.h>
#include <QuadTree.h>
#include <UvStucco.h>
#include <Usg.h>

typedef struct {
	char name[64];
	char type[2];
	int32_t sizeInBits;
} AttributeDesc;

typedef struct {
	char format[MAP_FORMAT_NAME_MAX_LEN];
	int32_t version;
	int64_t dataSize;
	int64_t dataSizeCompressed;
	int32_t objCount;
	int32_t usgCount;
	int32_t flatCutoffCount;
} StucHeader;

typedef struct StucMapInternal {
	Mesh mesh;
	QuadTree quadTree;
	UsgArr usgArr;
	V2_F32 zBounds;
	StucAttribIndexedArr indexedAttribs;
} MapFile;
