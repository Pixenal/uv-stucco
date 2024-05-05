#pragma once
#include <Types.h>

typedef struct {
	int32_t cellFacesTotal;
	int32_t cellFacesMax;
	FaceCellsInfo *pFaceCellsInfo;
	int32_t*pCellFaces;
	int32_t uniqueFaces;
} EnclosingCells;

void ruvmGetEnclosingCells(RuvmAllocator *pAlloc, RuvmMap pMap,
                           Mesh *pMeshIn, EnclosingCells *pEc);
void ruvmDestroyEnclosingCells(RuvmAllocator *pAlloc, EnclosingCells *pEc);

