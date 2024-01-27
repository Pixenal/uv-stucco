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
	int32_t dataSize;
	int32_t dataSizeCompressed;
	int32_t vertAttributeSize;
	AttributeDesc *pVertAttributeDesc;
	int32_t loopAttributeSize;
	AttributeDesc *pLoopAttributeDesc;
} RuvmHeader;

typedef struct {
	RuvmHeader header;
	MeshData mesh;
	QuadTree quadTree;
} RuvmFileLoaded;

void writeDebugImage(Cell *pRootCell);
void writeRuvmFile(MeshData *pMesh);
void loadRuvmFile(RuvmFileLoaded *pFileLoaded, char *pFilePath);
void destroyRuvmFile(RuvmFileLoaded *pFileLoaded);
