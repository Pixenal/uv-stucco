#pragma once
#include <Types.h>
#include <RUVM.h>

typedef struct {
	int32_t cellFacesTotal;
	int32_t cellFacesMax;
	FaceCellsInfo *pFaceCellsInfo;
	int32_t*pCellFaces;
	int32_t uniqueFaces;
} EnclosingCells;

void ruvmMapToJobMesh(void *pArgsPtr);
void ruvmMapToSingleFace(MappingJobVars *pArgs, EnclosingCells *pEcVars,
                         DebugAndPerfVars *pDpVars, V2_F32 fTileMin, int32_t tile,
						 FaceInfo baseFace);
void ruvmGetEnclosingCells(RuvmAllocator *pAlloc, RuvmMap pMap,
                           Mesh *pMeshIn, EnclosingCells *pEc);
void ruvmDestroyEnclosingCells(RuvmAllocator *pAlloc, EnclosingCells *pEc);
