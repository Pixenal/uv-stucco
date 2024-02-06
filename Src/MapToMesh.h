#pragma once
#include <Types.h>
#include <RUVM.h>
#include <EnclosingCells.h>

typedef struct {
	uint32_t vertAdjSize;
	VertAdj *pRuvmVertAdj;
} MapToMeshVars;

void ruvmMapToSingleFace(ThreadArg *pArgs, EnclosingCellsVars *pEcVars,
                         MapToMeshVars *pMmVars, DebugAndPerfVars *pDpVars,
                         RuvmVec2 fTileMin, int32_t tile, FaceInfo baseFace);
