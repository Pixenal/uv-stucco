#pragma once
#include <Types.h>
#include <RUVM.h>
#include <EnclosingCells.h>

typedef struct {
	uint32_t vertAdjSize;
	VertAdj *pRuvmVertAdj;
	int32_t edgeTableSize;
	MeshBufEdgeTable *pEdgeTable;
	Mat3x3 tbn;
	FaceTriangulated faceTriangulated;
} MapToMeshVars;

void ruvmMapToSingleFace(ThreadArg *pArgs, EnclosingCells *pEcVars,
                         MapToMeshVars *pMmVars, DebugAndPerfVars *pDpVars,
                         V2_F32 fTileMin, int32_t tile, FaceInfo baseFace);
