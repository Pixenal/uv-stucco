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
	AttributeDesc *pVertAttributeDesc;
	int32_t loopAttributeAmount;
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
