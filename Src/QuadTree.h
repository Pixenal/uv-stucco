#pragma once
#include "Types.h"

#define CELL_MAX_VERTS 32

typedef struct {
	Cell *pRootCell;
	int32_t edgeSize;
	int32_t *pEdges;
	int32_t edgeFaceSize;
	int32_t *pEdgeFaces;
	int32_t maxTreeDepth;
	int32_t cellCount;
	int32_t leafCount;
} QuadTree;

typedef struct {
	int32_t cellSize;
	Cell **ppCells;
	int8_t *pCellType;
	int32_t faceTotal;
	int32_t faceTotalNoDup;
} EnclosingCellsInfo;

void ruvmGetAllEnclosingCells(RuvmAllocator* pAlloc, Cell *pRootCell, EnclosingCellsInfo *pEnclosingCellsInfo,
                                  int8_t *pCellInits, Mesh *pMesh, FaceInfo faceInfo,
								  V2_I32 tileMin);
void ruvmCreateQuadTree(RuvmContext pContext, RuvmMap pMap);
void ruvmDestroyQuadTree(RuvmContext pContext, Cell *rootCell);
