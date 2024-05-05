#pragma once
#include <RUVM.h>
#include <Types.h>

typedef struct Piece {
	struct Piece *pNext;
	BoundaryVert *pEntry;
	BoundaryVert *pTail;
	int32_t listed;
	int32_t edgeCount;
	int32_t edges[11];
	int32_t entryIndex;
} Piece;

typedef struct {
	EdgeTable *pEdgeTable;
	OnLineTable *pOnLineTable;
	SeamEdgeTable *pSeamEdgeTable;
	int32_t edgeTableSize;
	int32_t onLineTableSize;
	int32_t seamTableSize;
} CombineTables;

void ruvmMergeBoundaryFaces(RuvmContext pContext, RuvmMap pMap, Mesh *pMeshOut,
                            SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
							JobBases *pJobBases, int8_t *pVertSeamTable);

void ruvmMergeSingleBoundsFace(uint64_t *pTimeSpent, RuvmContext pContext,
                               RuvmMap pMap, Mesh *pMeshOut,
							   SendOffArgs *pJobArgs, Piece *pPiece,
							   CombineTables *pCTables, JobBases *pJobBases,
							   int8_t *pVertSeamTable, int32_t entryNum,
							   FaceInfo *pRuvmFace);
void ruvmCombineJobMeshes(RuvmContext pContext, RuvmMap pMap,  RuvmMesh *pMeshOut,
                          SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
						  int8_t *pVertSeamTable);
