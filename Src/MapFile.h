#pragma once
#include <stdint.h>

#include <Io.h>
#include <QuadTree.h>
#include <UvStucco.h>
#include <Usg.h>
#include <Types.h>

typedef struct {
	char name[64];
	char type[2];
	I32 sizeInBits;
} AttributeDesc;

typedef struct {
	char format[MAP_FORMAT_NAME_MAX_LEN];
	I64 dataSize;
	I64 dataSizeCompressed;
	I32 version;
	I32 objCount;
	I32 usgCount;
	I32 flatCutoffCount;
} StucHeader;

typedef struct StucMapInternal {
	UsgArr usgArr;
	const Mesh *pMesh;
	QuadTree quadTree;
	StucAttribIndexedArr indexedAttribs;
	V2_F32 zBounds;
} MapFile;
