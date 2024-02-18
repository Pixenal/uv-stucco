#pragma once
#include <Types.h>

typedef struct {
	int32_t cellFacesTotal;
	int32_t cellFacesMax;
	FaceCellsInfo *pFaceCellsInfo;
	int32_t*pCellFaces;
	int32_t averageRuvmFacesPerFace;
	int8_t *pCellInits;
	FaceInfo faceInfo;
	FaceBounds faceBounds;
	int8_t *pCellTable;
	int32_t uniqueFaces;
	Cell **ppCells;
	int8_t *pCellType;
} EnclosingCellsVars;

void ruvmGetEnclosingCellsForAllFaces(ThreadArg *pArgs, EnclosingCellsVars *pEcVars);

