#pragma once
#include <RUVM.h>
#include <RuvmInternal.h>
#include <Utils.h>
#include <QuadTree.h>

typedef struct Piece {
	struct Piece *pNext;
	BorderFace *pEntry;
	BorderFace *pTail;
	int32_t listed;
	int32_t edgeCount;
	int32_t edges[11];
	int32_t entryIndex;
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
	int8_t keepBaseLoop;
	int32_t job;
	int32_t loopIndex;
	int32_t loop;
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

void ruvmMergeSingleBorderFace(uint64_t *pTimeSpent, RuvmContext pContext,
                               RuvmMap pMap, Mesh *pMeshOut,
							   SendOffArgs *pJobArgs, Piece *pPiece,
							   CombineTables *pCTables, JobBases *pJobBases,
							   int8_t *pVertSeamTable, int32_t entryNum,
							   FaceInfo *pRuvmFace);
void ruvmCombineJobMeshes(RuvmContext pContext, RuvmMap pMap,  RuvmMesh *pMeshOut,
                          SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
						  int8_t *pVertSeamTable);
