#pragma once
#include <RUVM.h>
#include <RuvmInternal.h>
#include <Utils.h>
#include <QuadTree.h>

typedef struct {
	void *pLoopBuf;
	void *pSeamBuf;
	void *pMapLoopBuf;
	int32_t size;
} MergeBufHandles;

typedef struct Piece {
	struct Piece *pNext;
	BorderFace *pEntry;
	BorderFace *pTail;
	int32_t edgeCount;
	int32_t edges[11];
	int32_t entryIndex;
	int16_t pKeep;
	int16_t pBaseKeep;
	int8_t listed;
} Piece;

typedef struct BorderEdge {
	struct BorderEdge *pNext;
	int32_t edge;
	int32_t inEdge;
	int32_t mapFace;
	int32_t valid;
} BorderEdge;

typedef struct OnLine {
	struct OnLine *pNext;
	int32_t baseEdgeOrLoop;
	int32_t ruvmVert;
	int32_t outVert;
	int32_t type;
} OnLine;

typedef struct BorderVert {
	struct BorderVert *pNext;
	int32_t ruvmFace;
	int32_t ruvmEdge;
	int32_t vert;
	int32_t tile;
	int32_t loops;
	int32_t baseEdge;
	int32_t baseVert;
	int32_t loopIndex;
	int32_t loop;
	int8_t job;
	int8_t keepBaseLoop;
} BorderVert;

typedef struct {
	BorderVert *pVertTable;
	OnLine *pOnLineTable;
	BorderEdge *pEdgeTable;
	int32_t vertTableSize;
	int32_t onLineTableSize;
	int32_t edgeTableSize;
} CombineTables;

typedef struct {
	int32_t vertBase;
	int32_t edgeBase;
} JobBases;

void ruvmMergeBorderFaces(RuvmContext pContext, RuvmMap pMap, Mesh *pMeshOut,
                          SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
						  JobBases *pJobBases, int8_t *pVertSeamTable);

void ruvmAllocMergeBufs(RuvmContext pContext, MergeBufHandles *pHandle,
                        int32_t totalVerts);
void ruvmMergeSingleBorderFace(uint64_t *pTimeSpent, RuvmContext pContext,
                               RuvmMap pMap, Mesh *pMeshOut,
							   SendOffArgs *pJobArgs, Piece *pPiece,
							   CombineTables *pCTables, JobBases *pJobBases,
							   FaceInfo *pRuvmFace, MergeBufHandles *pMergeBufHandles);
void ruvmDestroyMergeBufs(RuvmContext pContext, MergeBufHandles *pHandle);
void ruvmCombineJobMeshes(RuvmContext pContext, RuvmMap pMap,  RuvmMesh *pMeshOut,
                          SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
						  int8_t *pVertSeamTable);
